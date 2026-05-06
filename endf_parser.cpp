#include "endf_parser.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace endf
{

void read_tape(FILE *lib, isotope &iso);
void read_1_451(FILE *lib, isotope &iso, std::vector<fsection> &fsections);
void read_3(FILE *lib, csection &cs, int32_t mt);

void read_cont(FILE *lib, cont &c);
void read_tab1(FILE *lib, tab1 &t);

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

void read_cont(FILE *lib, cont &c)
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
    l.mat               = parse_int(ptr, MAT_WIDTH);
    ptr                += MAT_WIDTH;
    l.mf                = parse_int(ptr, MF_WIDTH);
    ptr                += MF_WIDTH;
    l.mt                = parse_int(ptr, MT_WIDTH);

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
