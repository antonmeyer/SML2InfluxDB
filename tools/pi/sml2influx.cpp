// sml2influx - reads an SML power meter on a serial port and writes it to InfluxDB 2 (line protocol over HTTP).
//
// Same decoder and same data reduction as the ESP32 firmware (src/MeterData.h, src/SampleAggregator.h), so the
// data of a meter on the Raspberry Pi looks exactly like the data of a meter on an ESP32: tag eMeter, fields actW /
// actWmin / actWmax (if the meter sends the power), sumWh, expWh, time stamp from the clock of the meter.
//
//   sml2influx -c /etc/sml2influx.conf                 live, reads the serial port
//   sml2influx -c ... --dry-run                         live, but prints the lines instead of sending them
//   sml2influx -c ... --replay capture.hex              test: frames from a file (hex text or binary), always dry
//
// Build: see Makefile. Configuration: sml2influx.conf.example.

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <deque>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include "FrameAssembler.h"
#include "MeterData.h"
#include "SampleAggregator.h"

// ---- configuration --------------------------------------------------------------------------------------------

struct Config
{
    std::string device = "/dev/ttyWP";
    std::string influxUrl = "http://localhost:8086"; // plain HTTP only (the database is on the same machine)
    std::string org, bucket, token;
    std::string measurement = "ePower_test";
    std::string alias;                 // eMeter tag if the meter is not in the table of MeterData.h
    std::string extraTags;             // more tags, "key=value,key2=value2"
    uint32_t interval = 5;             // window in seconds, 0: every frame
    uint32_t heartbeat = 30;           // energy is sent at the latest after this
    uint32_t batch = 20;               // lines per request
    uint32_t flushSeconds = 10;        // a request at the latest after this
    bool dryRun = false;
};

static std::string trim(const std::string &s)
{
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

static bool loadConfig(const char *path, Config &c)
{
    std::ifstream f(path);
    if (!f)
        return false;
    std::string line;
    while (std::getline(f, line))
    {
        size_t h = line.find('#');
        if (h != std::string::npos)
            line.erase(h);
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
        if (k == "device") c.device = v;
        else if (k == "influx_url") c.influxUrl = v;
        else if (k == "org") c.org = v;
        else if (k == "bucket") c.bucket = v;
        else if (k == "token") c.token = v;
        else if (k == "measurement") c.measurement = v;
        else if (k == "alias") c.alias = v;
        else if (k == "extra_tags") c.extraTags = v;
        else if (k == "interval") c.interval = (uint32_t)atoi(v.c_str());
        else if (k == "heartbeat") c.heartbeat = (uint32_t)atoi(v.c_str());
        else if (k == "batch") c.batch = (uint32_t)atoi(v.c_str());
        else if (k == "flush_seconds") c.flushSeconds = (uint32_t)atoi(v.c_str());
        else if (k == "dry_run") c.dryRun = atoi(v.c_str()) != 0;
        else fprintf(stderr, "config: unknown key '%s'\n", k.c_str());
    }
    return true;
}

// ---- logging --------------------------------------------------------------------------------------------------

static void logf(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stderr, "%s\n", buf); // systemd adds the time
    fflush(stderr);
}

// ---- line protocol --------------------------------------------------------------------------------------------

static std::string escapeTag(const std::string &s)
{
    std::string o;
    for (char ch : s)
    {
        if (ch == ',' || ch == ' ' || ch == '=')
            o += '\\';
        o += ch;
    }
    return o;
}

static std::string makeLine(const Config &c, const char *alias, const SamplePoint &p)
{
    char b[64];
    std::string l = escapeTag(c.measurement) + ",eMeter=" + escapeTag(alias);
    if (!c.extraTags.empty())
        l += "," + c.extraTags;
    l += " ";
    bool first = true;
    auto field = [&](const char *name, double v) {
        snprintf(b, sizeof(b), "%s%s=%.1f", first ? "" : ",", name, v);
        l += b;
        first = false;
    };
    if (p.hasW)
    {
        field("actW", p.wMean);
        field("actWmin", p.wMin);
        field("actWmax", p.wMax);
    }
    if (p.hasSumWh)
        field("sumWh", p.sumWh);
    if (p.hasExpWh)
        field("expWh", p.expWh);
    snprintf(b, sizeof(b), " %u", (unsigned)p.epoch);
    return l + b;
}

// ---- InfluxDB writer ------------------------------------------------------------------------------------------

static std::string urlEncode(const std::string &s)
{
    std::string o;
    char b[4];
    for (unsigned char ch : s)
    {
        if (isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~')
            o += (char)ch;
        else
        {
            snprintf(b, sizeof(b), "%%%02X", ch);
            o += b;
        }
    }
    return o;
}

class InfluxWriter
{
public:
    explicit InfluxWriter(const Config &c) : c_(c)
    {
        std::string u = c.influxUrl;
        if (u.compare(0, 7, "http://") == 0)
            u = u.substr(7);
        size_t colon = u.find(':');
        host_ = u.substr(0, colon);
        port_ = colon == std::string::npos ? "80" : u.substr(colon + 1);
    }

    void add(const std::string &line)
    {
        if (c_.dryRun)
        {
            printf("%s\n", line.c_str());
            fflush(stdout);
            sent_++;
            return;
        }
        if (q_.empty())
            oldest_ = time(nullptr);
        q_.push_back(line);
        if (q_.size() > MAX_QUEUE)
        {
            q_.pop_front();
            dropped_++;
        }
    }

    // sends when a batch is full or the oldest line is old enough; force: also a smaller rest (shutdown)
    void flush(bool force = false)
    {
        if (q_.empty() || time(nullptr) < retryAt_)
            return;
        if (!force && q_.size() < c_.batch && (uint32_t)(time(nullptr) - oldest_) < c_.flushSeconds)
            return;
        std::string body;
        size_t n = 0;
        for (; n < q_.size() && n < MAX_BATCH; n++)
            body += q_[n] + "\n";
        int status = post(body);
        if (status == 204)
        {
            q_.erase(q_.begin(), q_.begin() + n);
            sent_ += n;
            failures_ = 0;
            oldest_ = time(nullptr);
        }
        else if (status >= 400 && status < 500)
        {
            // the server does not like these lines (or the token): retrying would not help, do not block the rest
            logf("influx: HTTP %d, %zu lines dropped. First: %s", status, n, q_.front().c_str());
            q_.erase(q_.begin(), q_.begin() + n);
            dropped_ += n;
        }
        else
        {
            failures_++;
            errors_++;
            retryAt_ = time(nullptr) + (failures_ > 5 ? 60 : (1 << failures_));
            logf("influx: write failed (%s), %zu lines waiting, retry in %ld s", status < 0 ? strerror(errno) : "server error",
                 q_.size(), (long)(retryAt_ - time(nullptr)));
        }
    }

    size_t queued() const { return q_.size(); }
    unsigned long sent() const { return sent_; }
    unsigned long errors() const { return errors_; }
    unsigned long dropped() const { return dropped_; }

private:
    static const size_t MAX_QUEUE = 20000, MAX_BATCH = 500;
    const Config &c_;
    std::string host_, port_;
    std::deque<std::string> q_;
    time_t oldest_ = 0, retryAt_ = 0;
    unsigned failures_ = 0;
    unsigned long sent_ = 0, errors_ = 0, dropped_ = 0;

    // HTTP/1.1 POST over a plain socket. Returns the HTTP status, -1 if the connection failed.
    int post(const std::string &body)
    {
        struct addrinfo hints, *res = nullptr;
        memset(&hints, 0, sizeof(hints));
        hints.ai_socktype = SOCK_STREAM;
        if (getaddrinfo(host_.c_str(), port_.c_str(), &hints, &res) != 0 || !res)
            return -1;
        int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        struct timeval tv = {10, 0};
        if (fd >= 0)
        {
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        }
        if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0)
        {
            if (fd >= 0)
                close(fd);
            freeaddrinfo(res);
            return -1;
        }
        freeaddrinfo(res);

        std::ostringstream req;
        req << "POST /api/v2/write?org=" << urlEncode(c_.org) << "&bucket=" << urlEncode(c_.bucket)
            << "&precision=s HTTP/1.1\r\nHost: " << host_ << ":" << port_ << "\r\nAuthorization: Token " << c_.token
            << "\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " << body.size()
            << "\r\nConnection: close\r\n\r\n"
            << body;
        std::string r = req.str();
        size_t off = 0;
        while (off < r.size())
        {
            ssize_t w = send(fd, r.data() + off, r.size() - off, MSG_NOSIGNAL);
            if (w <= 0)
            {
                close(fd);
                return -1;
            }
            off += (size_t)w;
        }
        char buf[1024];
        std::string resp;
        ssize_t n;
        while ((n = recv(fd, buf, sizeof(buf), 0)) > 0 && resp.size() < 4096)
            resp.append(buf, (size_t)n);
        close(fd);
        int status = -1;
        if (resp.compare(0, 5, "HTTP/") == 0 && resp.size() > 12)
            status = atoi(resp.c_str() + 9);
        if (status >= 400)
        {
            size_t b = resp.find("\r\n\r\n");
            logf("influx answer: %s", b == std::string::npos ? "?" : resp.substr(b + 4, 300).c_str());
        }
        return status;
    }
};

// ---- serial port ----------------------------------------------------------------------------------------------

static int openSerial(const std::string &dev)
{
    int fd = open(dev.c_str(), O_RDONLY | O_NOCTTY | O_NONBLOCK);
    if (fd < 0)
        return -1;
    struct termios t;
    if (tcgetattr(fd, &t) != 0)
    {
        close(fd);
        return -1;
    }
    cfmakeraw(&t);
    cfsetispeed(&t, B9600);
    cfsetospeed(&t, B9600);
    t.c_cflag |= CLOCAL | CREAD;
    t.c_cflag &= ~(PARENB | CSTOPB | CSIZE | CRTSCTS);
    t.c_cflag |= CS8; // 8N1
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &t);
    tcflush(fd, TCIFLUSH);
#ifdef TIOCEXCL
    ioctl(fd, TIOCEXCL); // nobody else may open it from now on
#endif
    return fd;
}

// ---- main -----------------------------------------------------------------------------------------------------

static volatile sig_atomic_t stopFlag = 0;
static void onSignal(int) { stopFlag = 1; }

struct Stats
{
    unsigned long frames = 0, bad = 0, noData = 0, points = 0;
};

// one frame: check, decode, reduce, queue the line
static void handleFrame(const uint8_t *f, size_t n, const Config &c, MeterDataSet &ds, SampleAggregator &agg, InfluxWriter &w,
                        Stats &st, uint32_t nowEpoch)
{
    SmlFrameStatus s = smlDecodeFrame(f, n, ds);
    if (s != SML_FRAME_OK)
    {
        st.bad++;
        if (st.bad <= 5 || st.bad % 100 == 0)
            logf("frame rejected (status %d, %zu bytes), %lu so far", (int)s, n, st.bad);
        return;
    }
    if (!ds.hasData())
    {
        st.noData++;
        return;
    }
    st.frames++;
    SamplePoint p;
    if (agg.add(ds, nowEpoch, p))
    {
        st.points++;
        w.add(makeLine(c, ds.getalias(), p));
    }
}

static bool readFile(const char *path, std::string &out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;
    std::stringstream ss;
    ss << f.rdbuf();
    std::string d = ss.str(), hex;
    bool isHex = !d.empty();
    for (char ch : d)
        if (!isxdigit((unsigned char)ch) && !isspace((unsigned char)ch))
        {
            isHex = false;
            break;
        }
    if (!isHex)
    {
        out = d;
        return true;
    }
    for (char ch : d)
        if (!isspace((unsigned char)ch))
            hex += ch;
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
        out += (char)strtol(hex.substr(i, 2).c_str(), nullptr, 16);
    return true;
}

int main(int argc, char **argv)
{
    Config c;
    const char *confPath = nullptr, *replay = nullptr;
    bool dry = false;
    for (int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        if ((a == "-c" || a == "--config") && i + 1 < argc) confPath = argv[++i];
        else if (a == "--dry-run") dry = true;
        else if (a == "--replay" && i + 1 < argc) replay = argv[++i];
        else if (a == "--device" && i + 1 < argc) c.device = argv[++i];
        else
        {
            fprintf(stderr, "usage: %s [-c conf] [--dry-run] [--replay file] [--device dev]\n", argv[0]);
            return 2;
        }
    }
    if (confPath && !loadConfig(confPath, c))
    {
        fprintf(stderr, "cannot read %s\n", confPath);
        return 2;
    }
    // device given on the command line wins, so reload would overwrite: apply the flags after the file
    for (int i = 1; i < argc; i++)
        if (std::string(argv[i]) == "--device" && i + 1 < argc)
            c.device = argv[i + 1];
    if (dry || replay)
        c.dryRun = true;
    if (!c.dryRun && (c.org.empty() || c.bucket.empty() || c.token.empty()))
    {
        fprintf(stderr, "org, bucket and token must be set in the configuration (or use --dry-run)\n");
        return 2;
    }

    MeterDataSet ds;
    ds.alias = c.alias.empty() ? nullptr : c.alias.c_str();
    SampleAggregator agg(c.interval, c.heartbeat);
    InfluxWriter writer(c);
    FrameAssembler fa;
    Stats st;

    if (replay) // test mode: the clock is made up from the meter time, so the output is the same every time
    {
        std::string data;
        if (!readFile(replay, data))
        {
            fprintf(stderr, "cannot read %s\n", replay);
            return 2;
        }
        uint32_t base = 1760000000u, first = 0;
        bool haveFirst = false;
        fa.feed((const uint8_t *)data.data(), data.size(), [&](const uint8_t *f, size_t n) {
            // peek the meter time to move the made-up clock the same way as the meter's clock
            MeterDataSet tmp;
            if (smlDecodeFrame(f, n, tmp) == SML_FRAME_OK && tmp.hasSensorTime)
            {
                if (!haveFirst)
                {
                    first = tmp.sensorTime;
                    haveFirst = true;
                }
                handleFrame(f, n, c, ds, agg, writer, st, base + (tmp.sensorTime - first));
            }
            else
                handleFrame(f, n, c, ds, agg, writer, st, base);
        });
        logf("replay: %lu frames, %lu rejected, %lu points, junk bytes %zu, cut frames %zu", st.frames, st.bad, st.points,
             fa.junkBytes(), fa.cutFrames());
        return 0;
    }

    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);
    signal(SIGPIPE, SIG_IGN);
    logf("sml2influx started: %s -> %s %s/%s, window %u s, heartbeat %u s%s", c.device.c_str(), c.influxUrl.c_str(),
         c.bucket.c_str(), c.measurement.c_str(), c.interval, c.heartbeat, c.dryRun ? ", DRY RUN" : "");

    int fd = -1;
    time_t lastStatus = time(nullptr), lastOpenTry = 0;
    uint8_t buf[512];
    while (!stopFlag)
    {
        if (fd < 0)
        {
            if (time(nullptr) - lastOpenTry >= 5)
            {
                lastOpenTry = time(nullptr);
                fd = openSerial(c.device);
                if (fd < 0)
                    logf("cannot open %s: %s", c.device.c_str(), strerror(errno));
                else
                    logf("%s open", c.device.c_str());
            }
            if (fd < 0)
            {
                usleep(500000);
                writer.flush();
                continue;
            }
        }
        struct pollfd p = {fd, POLLIN, 0};
        int r = poll(&p, 1, 1000);
        if (r > 0)
        {
            ssize_t n = read(fd, buf, sizeof(buf));
            if (n > 0)
                fa.feed(buf, (size_t)n, [&](const uint8_t *f, size_t len) {
                    handleFrame(f, len, c, ds, agg, writer, st, (uint32_t)time(nullptr));
                });
            else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR))
            {
                logf("read error on %s: %s", c.device.c_str(), strerror(errno));
                close(fd);
                fd = -1;
            }
        }
        writer.flush();
        if (time(nullptr) - lastStatus >= 60)
        {
            lastStatus = time(nullptr);
            logf("alive: frames %lu, rejected %lu, points %lu, sent %lu, queued %zu, errors %lu, dropped %lu, junk bytes %zu",
                 st.frames, st.bad, st.points, writer.sent(), writer.queued(), writer.errors(), writer.dropped(), fa.junkBytes());
        }
    }
    writer.flush(true);
    logf("sml2influx stopped");
    if (fd >= 0)
        close(fd);
    return 0;
}
