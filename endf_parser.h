#ifndef ENDF_PARSER_H
#define ENDF_PARSER_H

#include <cstdint>
#include <cstdio>
#include <vector>

namespace endf
{

const int16_t PARSE_BUFFER_MAX = 16;

const int16_t LINE_WIDTH     = 80 + 2;
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

    std::vector<int32_t> nbound;
    std::vector<int32_t> interp;
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

    std::vector<int32_t> nbound;
    std::vector<int32_t> interp;
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

void read_tape(FILE *lib, isotope &iso);
void read_1_451(FILE *lib, isotope &iso, std::vector<fsection> &fsections);
void read_3(FILE *lib, csection &cs, int32_t mt);
void read_4_2(FILE *lib);

void read_cont(FILE *lib, cont &c);
void read_tab1(FILE *lib, tab1 &t);

void    read_line(FILE *lib, line &l);
void    skip_line(FILE *lib);
void    skip_to_fend(FILE *lib);
void    expect_send(FILE *lib);
void    expect_fend(FILE *lib);
double  parse_double(const char *const p);
int32_t parse_int(const char *const p, const int32_t len);

} // namespace endf

#endif
