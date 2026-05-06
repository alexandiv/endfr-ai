#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Конфигурация запуска
// ---------------------------------------------------------------------------

const int16_t PATH_LEN = 1024;

struct run_config
{
    std::string endf_path;     // путь к исходному endf-файлу
    std::string reconr_path;   // заполняется в run_reconr()
    std::string output_path;   // заполняется в run_reconr()
    int32_t     endf_mat;      // номер материала
    double      errmax;        // точность reconr
};

// Объявления
void       read_input(const char *path, run_config &cfg);
void       write_reconr_input(const run_config &cfg,
                              const char *inp_path);
void       run_reconr(run_config &cfg);

static std::string extract_basename(const std::string &path);
static std::string extract_dir(const std::string &path);

// Читает файл input: строка endf_path, endf_mat, errmax
void read_input(const char *path, run_config &cfg)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
    {
        fprintf(stderr, "- Error: cannot open input file '%s'\n", path);
        exit(EXIT_FAILURE);
    }

    char buf[PATH_LEN];
    char fmt[32];
    snprintf(fmt, sizeof(fmt), "%%%ds", PATH_LEN - 1);
    if (fscanf(f, fmt, buf) != 1)
    {
        fprintf(stderr, "- Error: cannot read endf_path from '%s'\n", path);
        fclose(f);
        exit(EXIT_FAILURE);
    }
    cfg.endf_path = buf;
    if (fscanf(f, " %d", &cfg.endf_mat) != 1)
    {
        fprintf(stderr, "- Error: cannot read endf_mat from '%s'\n", path);
        fclose(f);
        exit(EXIT_FAILURE);
    }
    if (fscanf(f, " %lf", &cfg.errmax) != 1)
    {
        fprintf(stderr, "- Error: cannot read errmax from '%s'\n", path);
        fclose(f);
        exit(EXIT_FAILURE);
    }

    fclose(f);
}

// Выделяет basename файла, убирая расширение
static std::string extract_basename(const std::string &path)
{
    auto slash = path.rfind('/');
    std::string base = (slash != std::string::npos)
                           ? path.substr(slash + 1)
                           : path;
    auto dot = base.rfind('.');
    if (dot != std::string::npos)
        base.erase(dot);
    return base;
}

// Выделяет директорию файла (с завершающим '/')
static std::string extract_dir(const std::string &path)
{
    auto slash = path.rfind('/');
    if (slash != std::string::npos)
        return path.substr(0, slash + 1);
    return "";
}

// Формирует текстовое поле: basename + " endfr-vXX"
static std::string reconr_label(const std::string &endf_path)
{
    return extract_basename(endf_path) + " endfr-vXX";
}

// Пишет входной файл для njoy (reconr)
void write_reconr_input(const run_config &cfg, const char *inp_path)
{
    FILE *f = fopen(inp_path, "w");
    if (f == NULL)
    {
        fprintf(stderr, "- Error: cannot create reconr input '%s'\n",
                inp_path);
        exit(EXIT_FAILURE);
    }

    std::string label = reconr_label(cfg.endf_path);

    fprintf(f, "reconr\n");
    fprintf(f, " 20 21\n");
    fprintf(f, "'%s'/\n", label.c_str());
    fprintf(f, "%d/\n", cfg.endf_mat);
    fprintf(f, "%g/\n", cfg.errmax);
    fprintf(f, "0/\n");
    fprintf(f, "stop\n");

    fclose(f);
}

// Создаёт symlink tape20 -> endf, запускает ./njoy < *.inp,
// переименовывает tape21 -> *.reconr, убирает за собой
void run_reconr(run_config &cfg)
{
    std::string dir   = extract_dir(cfg.endf_path);
    std::string name  = extract_basename(cfg.endf_path);

    std::string inp_path    = name + ".inp";
    std::string reconr_path = dir + name + ".reconr";
    std::string output_path = dir + name + ".output";

    write_reconr_input(cfg, inp_path.c_str());

    unlink("tape20");
    if (symlink(cfg.endf_path.c_str(), "tape20") != 0)
    {
        fprintf(stderr, "- Error: symlink tape20 -> '%s' failed\n",
                cfg.endf_path.c_str());
        exit(EXIT_FAILURE);
    }

    unlink("tape21");

    fprintf(stdout, "+ Running njoy reconr for mat=%d errmax=%g\n",
            cfg.endf_mat, cfg.errmax);

    std::string cmd = "njoy < " + inp_path;
    int ret = system(cmd.c_str());
    if (ret != 0)
    {
        fprintf(stderr, "- Error: njoy exited with code %d\n", ret);
        unlink("tape20");
        exit(EXIT_FAILURE);
    }

    if (rename("tape21", reconr_path.c_str()) != 0)
    {
        fprintf(stderr, "- Error: cannot rename tape21 -> '%s'\n",
                reconr_path.c_str());
        unlink("tape20");
        exit(EXIT_FAILURE);
    }

    fprintf(stdout, "+ reconr output: %s\n", reconr_path.c_str());

    // переименовываем output, если существует
    rename("output", output_path.c_str());

    // убираем symlink
    unlink("tape20");

    // сохраняем пути в конфиг
    cfg.reconr_path = reconr_path;
    cfg.output_path = output_path;
}

namespace endf
{

const int16_t PARSE_BUFFER_MAX = 16;

const int16_t LINE_WIDTH     = 80 + 2; // 80 ENDF + '\n' + '\0'
const int16_t DATA_WIDTH     = 66;
const int16_t MAT_WIDTH      = 4;
const int16_t MF_WIDTH       = 2;
const int16_t MT_WIDTH       = 3;
const int16_t NS_WIDTH       = 5;
const int16_t FIELD_WIDTH    = 11;
const int16_t PAIRS_PER_LINE = 3;

struct line
{
    char    data[DATA_WIDTH + 1];
    int32_t mat;
    int32_t mf;
    int32_t mt;

    line() : data{}, mat(0), mf(0), mt(0)
    {
    }
};

struct cont
{
    double  c1, c2;
    int32_t l1, l2, n1, n2;

    cont() : c1(0.0), c2(0.0), l1(0), l2(0), n1(0), n2(0)
    {
    }
};

struct tab1
{
    double  c1, c2;
    int32_t l1, l2;

    std::vector<int32_t> nbound; // границы диапазонов
    std::vector<int32_t> interp; // схемы интерполяции
    std::vector<double>  x;
    std::vector<double>  y;

    tab1() : c1(0.0), c2(0.0), l1(0), l2(0)
    {
    }
};

struct csection
{
    double  za, awr, qm, qi;
    int32_t mt;
    int32_t lr;
    int32_t nr;
    int32_t np;

    std::vector<int32_t> nbound; // верхние границы диапазонов
    std::vector<int32_t> interp; // типы интерполяции для них
    std::vector<double>  x;
    std::vector<double>  y;

    csection() : za(0.0), awr(0.0), qm(0), qi(0), mt(0), lr(0), nr(0), np(0)
    {
    }
};

struct fsection
{
    int32_t mf;
    int32_t mt;

    fsection() : mf(0), mt(0)
    {
    }
};

struct isotope
{
    double  za, awr, elis, sta;
    int32_t lrp;
    int32_t lfi;
    double  awi, emax, temp;
    int32_t nwd;
    int32_t nxc;

    std::vector<csection> csections;

    isotope()
        : za(0.0), awr(0.0), elis(0.0), sta(0.0), awi(0.0), emax(0.0),
          temp(0.0), lrp(0), lfi(0), nwd(0), nxc(0)
    {
    }
};

// endf parts
void read_tape(FILE *lib, isotope &iso);
void read_1_451(FILE *lib, isotope &iso, std::vector<fsection> &fsections);
void read_3(FILE *lib, csection &cs, int32_t mt);

// endf records
void read_cont(FILE *lib, cont &c);
void read_tab1(FILE *lib, tab1 &t);

// read and parse data
void    read_line(FILE *lib, line &l);
void    skip_line(FILE *lib);
void    skip_to_fend(FILE *lib);
void    expect_send(FILE *lib);
void    expect_fend(FILE *lib);
double  parse_double(const char *const p);
int32_t parse_int(const char *const p, const int32_t len);

void read_tape(FILE *lib, isotope &iso)
{
    fprintf(stdout, "+ Start tape read\n");

    // пропускаем TPID
    skip_line(lib);

    // оглавление
    std::vector<fsection> fsections;

    // читаем mf = 1, mt = 451 (описание и оглавление)
    read_1_451(lib, iso, fsections);

    // SEND после MF=1/MT=451, затем FEND для MF=1
    expect_send(lib);
    expect_fend(lib);

    // считаем реакции MF=3 и выделяем память
    int32_t nreac = 0;
    for (size_t i = 0; i < fsections.size(); i++)
    {
        if (fsections[i].mf == 3)
            nreac++;
    }
    iso.csections.reserve(nreac);

    // заполняем
    for (size_t i = 0; i < fsections.size(); i++)
    {
        int32_t mf = fsections[i].mf;
        int32_t mt = fsections[i].mt;

        if (mf == 1 && mt == 451)
            continue;

        if (mf != 3)
        {
            skip_to_fend(lib);
            continue;
        }

        csection cs;
        read_3(lib, cs, mt);
        iso.csections.push_back(std::move(cs));
    }

    // FEND после последней секции MF=3
    expect_fend(lib);
}

void read_3(FILE *lib, csection &cs, int32_t mt)
{
    line l;
    cont c;

    cs.mt = mt;

    // HEAD: ZA, AWR, 0, 0, 0, 0
    read_cont(lib, c);
    cs.za  = c.c1;
    cs.awr = c.c2;

    // TAB1 head: QM, QI, 0, LR, NR, NP
    read_cont(lib, c);
    cs.qm = c.c1;
    cs.qi = c.c2;
    cs.lr = c.l2;
    cs.nr = c.n1;
    cs.np = c.n2;

    // NR пар (nbound, interp)
    if (cs.nr > 0)
    {
        cs.nbound.resize(cs.nr);
        cs.interp.resize(cs.nr);

        int32_t count = 0;
        while (count < cs.nr)
        {
            read_line(lib, l);
            const char *p = l.data;

            for (int i = 0; i < PAIRS_PER_LINE && count < cs.nr; i++)
            {
                cs.nbound[count]  = parse_int(p, FIELD_WIDTH);
                p                += FIELD_WIDTH;
                cs.interp[count]  = parse_int(p, FIELD_WIDTH);
                p                += FIELD_WIDTH;
                count++;
            }
        }
    }

    // NP пар (x, y)
    if (cs.np > 0)
    {
        cs.x.resize(cs.np);
        cs.y.resize(cs.np);

        int32_t count = 0;
        while (count < cs.np)
        {
            read_line(lib, l);
            const char *p = l.data;

            for (int i = 0; i < PAIRS_PER_LINE && count < cs.np; i++)
            {
                cs.x[count]  = parse_double(p);
                p           += FIELD_WIDTH;
                cs.y[count]  = parse_double(p);
                p           += FIELD_WIDTH;
                count++;
            }
        }
    }

    // SEND после секции
    expect_send(lib);
}

void read_1_451(FILE *lib, isotope &iso, std::vector<fsection> &fsections)
{
    line l;
    cont c;

    read_cont(lib, c);
    iso.za  = c.c1;
    iso.awr = c.c2;
    iso.lrp = c.l1;
    iso.lfi = c.l2;

    read_cont(lib, c);
    iso.elis = c.c1;
    iso.sta  = c.c2;

    read_cont(lib, c);
    iso.awi  = c.c1;
    iso.emax = c.c2;

    read_cont(lib, c);
    iso.temp = c.c1;
    iso.nwd  = c.n1;
    iso.nxc  = c.n2;

    for (int i = 0; i < iso.nwd; i++)
        read_line(lib, l);

    fsections.resize(iso.nxc);
    for (int i = 0; i < iso.nxc; i++)
    {
        read_line(lib, l);
        char *p = l.data;

        p               += 2 * FIELD_WIDTH;
        fsections[i].mf  = parse_int(p, FIELD_WIDTH);
        p               += FIELD_WIDTH;
        fsections[i].mt  = parse_int(p, FIELD_WIDTH);
    }
}

void read_cont(FILE *lib, struct cont &c)
{
    line l;
    read_line(lib, l);

    const char *p  = l.data;
    c.c1           = parse_double(p);
    p             += FIELD_WIDTH;
    c.c2           = parse_double(p);
    p             += FIELD_WIDTH;
    c.l1           = parse_int(p, FIELD_WIDTH);
    p             += FIELD_WIDTH;
    c.l2           = parse_int(p, FIELD_WIDTH);
    p             += FIELD_WIDTH;
    c.n1           = parse_int(p, FIELD_WIDTH);
    p             += FIELD_WIDTH;
    c.n2           = parse_int(p, FIELD_WIDTH);
}

void read_tab1(FILE *lib, tab1 &t)
{
    line l;
    cont c;
    read_cont(lib, c);

    t.c1 = c.c1;
    t.c2 = c.c2;
    t.l1 = c.l1;
    t.l2 = c.l2;

    int32_t nr = c.n1; // число диапазонов интерполяции
    int32_t np = c.n2; // число пар (x, y)

    // Чтение интерполяции: NR пар (nbound, interp)
    if (nr > 0)
    {
        t.nbound.resize(nr);
        t.interp.resize(nr);

        int32_t count = 0;
        while (count < nr)
        {
            read_line(lib, l);
            const char *p = l.data;

            // 6 полей по FIELD_WIDTH = PAIRS_PER_LINE пар (nbt, int) на строку
            for (int i = 0; i < PAIRS_PER_LINE && count < nr; i++)
            {
                t.nbound[count]  = parse_int(p, FIELD_WIDTH);
                p               += FIELD_WIDTH;
                t.interp[count]  = parse_int(p, FIELD_WIDTH);
                p               += FIELD_WIDTH;
                count++;
            }
        }
    }

    // Чтение данных: NP пар (x, y)
    if (np > 0)
    {
        t.x.resize(np);
        t.y.resize(np);

        int32_t count = 0;
        while (count < np)
        {
            read_line(lib, l);
            const char *p = l.data;

            // 6 полей по FIELD_WIDTH = PAIRS_PER_LINE пар (x, y) на строку
            for (int i = 0; i < PAIRS_PER_LINE && count < np; i++)
            {
                t.x[count]  = parse_double(p);
                p          += FIELD_WIDTH;
                t.y[count]  = parse_double(p);
                p          += FIELD_WIDTH;
                count++;
            }
        }
    }
}

void read_line(FILE *lib, line &l)
{
    char str[LINE_WIDTH];
    memset(str, ' ', LINE_WIDTH - 1);
    str[LINE_WIDTH - 1] = '\0';

    if (fgets(str, LINE_WIDTH, lib) == NULL)
    {
        fprintf(stderr, "- Error in read_line()\n");
        exit(EXIT_FAILURE);
    }

    const char *ptr = str;

    memcpy(l.data, ptr, DATA_WIDTH);
    l.data[DATA_WIDTH]  = 0;
    ptr                += DATA_WIDTH;
    l.mat  = parse_int(ptr, MAT_WIDTH);
    ptr   += MAT_WIDTH;
    l.mf   = parse_int(ptr, MF_WIDTH);
    ptr   += MF_WIDTH;
    l.mt   = parse_int(ptr, MT_WIDTH);

    // NS field not parsed
}

void skip_line(FILE *lib)
{
    char str[LINE_WIDTH];

    if (fgets(str, LINE_WIDTH, lib) == NULL)
    {
        fprintf(stderr, "- Error in skip_line()\n");
        exit(EXIT_FAILURE);
    }
}

void skip_to_fend(FILE *lib)
{
    // читаем строки, пока не встретим FEND (mf == 0)
    line l;
    while (1)
    {
        read_line(lib, l);
        if (l.mf == 0)
            return;
    }
}

void expect_send(FILE *lib)
{
    line l;
    read_line(lib, l);
    if (l.mt != 0 || l.mf == 0)
    {
        fprintf(stderr, "- Error: expected SEND, got mf=%d mt=%d\n", l.mf,
                l.mt);
        exit(EXIT_FAILURE);
    }
}

void expect_fend(FILE *lib)
{
    line l;
    read_line(lib, l);
    if (l.mf != 0 || l.mt != 0)
    {
        fprintf(stderr, "- Error: expected FEND, got mf=%d mt=%d\n", l.mf,
                l.mt);
        exit(EXIT_FAILURE);
    }
}

double parse_double(const char *const ptr)
{
    char buf[PARSE_BUFFER_MAX]; // 11 + 'E' + '\0'
    bool e_inserted = false;
    int  j          = 0;

    // first symbol
    buf[j++] = ptr[0];

    // others plus E
    for (int i = 1; i < FIELD_WIDTH; i++)
    {
        if (ptr[i] == 'e' || ptr[i] == 'E')
        {
            fprintf(
                stderr,
                "- Error in parse_double() - found e or E in eless format\n");
            exit(EXIT_FAILURE);
        }
        if (!e_inserted && (ptr[i] == '+' || ptr[i] == '-'))
        {
            buf[j++]   = 'E';
            e_inserted = true;
        }
        if (j >= FIELD_WIDTH + 1)
        {
            fprintf(stderr, "- Error in parse_double() - buffer overflow\n");
            exit(EXIT_FAILURE);
        }
        buf[j++] = ptr[i];
    }
    buf[j] = '\0';

    // convert to double
    errno         = 0;
    char  *endptr = NULL;
    double result = strtod(buf, &endptr);
    if (endptr != buf + j || errno == ERANGE)
    {
        fprintf(stderr, "- Error in parse_double() - conversion failed\n");
        exit(EXIT_FAILURE);
    }

    return result;
}

int32_t parse_int(const char *const ptr, const int32_t len)
{
    if (len < 1 || len >= PARSE_BUFFER_MAX)
    {
        fprintf(stderr, "- Error in parse_int(): len is out of range\n");
        exit(EXIT_FAILURE);
    }

    char buf[PARSE_BUFFER_MAX];
    memcpy(buf, ptr, len);
    buf[len] = 0;

    errno        = 0;
    char *endptr = NULL;
    long  result = strtol(buf, &endptr, 10);
    if (endptr != buf + len || errno == ERANGE)
    {
        fprintf(stderr, "- Error in parse_int() - conversion failed\n");
        exit(EXIT_FAILURE);
    }
    if (result < INT32_MIN || result > INT32_MAX)
    {
        fprintf(stderr, "- Error in parse_int() - out of int32_t range\n");
        exit(EXIT_FAILURE);
    }
    return (int32_t)result;
}
} // namespace endf

int main()
{
    run_config cfg;
    read_input("input", cfg);

    run_reconr(cfg);

    // FILE         *lib = NULL;
    // endf::isotope iso;

    // lib = fopen("n-001_H_001.reconr", "r");
    // if (lib == NULL)
    // {
    //     fprintf(stderr, "- Error in fopen\n");
    //     exit(EXIT_FAILURE);
    // }
    // else
    // {
    //     fprintf(stdout, "+ Success in fopen\n");
    // }

    // endf::read_tape(lib, iso);
    // fclose(lib);

    // // сводка
    // fprintf(stdout, "+ isotope ZA=%.1f AWR=%.6f\n", iso.za, iso.awr);
    // fprintf(stdout, "+ csections: %zu\n", iso.csections.size());
    // fprintf(stdout, "  MT    QM           QI           Emin       Emax       Sig(0)     Sig(max)\n");
    // for (size_t i = 0; i < iso.csections.size(); i++)
    // {
    //     const endf::csection &cs = iso.csections[i];
    //     double x0 = cs.x.empty() ? 0.0 : cs.x[0];
    //     double xN = cs.x.empty() ? 0.0 : cs.x[cs.np - 1];
    //     double y0 = cs.y.empty() ? 0.0 : cs.y[0];
    //     double yN = cs.y.empty() ? 0.0 : cs.y[cs.np - 1];

    //     fprintf(stdout, "  %-3d  %-13.2e %-13.2e %-11.2e %-11.2e %-11.2e %-11.2e\n",
    //             cs.mt, cs.qm, cs.qi, x0, xN, y0, yN);
    // }

    return 0;
}
