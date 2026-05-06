#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>

#include "endf_parser.h"

// ---------------------------------------------------------------------------
// Конфигурация запуска
// ---------------------------------------------------------------------------

struct run_config
{
    std::string endf_path; // путь к исходному endf-файлу
    std::string endf_dir;
    std::string endf_name;
    std::string reconr_path;
    std::string output_path;
    int32_t     endf_mat; // номер материала
    double      err;      // точность сечения (обязательно)
    double      errmax;   // макс ошибка (умолчание: 10*err)
    double      errint;   // интерполяция ошибки (умолчание: err/20000)
    double      tempr;    // температура (умолчание: 0)
};

// Объявления
void read_input(const std::string &path, run_config &cfg);
void write_reconr_input(const run_config &cfg, const char *inp_path);
void run_reconr(run_config &cfg);

static std::string extract_basename(const std::string &path);
static std::string extract_dir(const std::string &path);
static bool        strip_comment(std::string &buf);
static bool        read_next_nonblank(std::ifstream &f, std::string &buf);

// Удаляет inline комментарий (!) и обрезает пробелы
static bool strip_comment(std::string &buf)
{
    // комментарии
    std::string::size_type pos = buf.find_first_of("!");
    if (pos != std::string::npos)
        buf.erase(pos);

    // ведущие пробелы
    std::string::size_type first = buf.find_first_not_of(" \t");
    if (first == std::string::npos)
    {
        buf.clear();
        return false;
    }
    buf.erase(0, first);

    // последние пробелы
    std::string::size_type last = buf.find_last_not_of(" \t");
    buf.erase(last + 1);
    return true;
}

static bool read_next_nonblank(std::ifstream &f, std::string &buf)
{
    while (std::getline(f, buf))
    {
        if (strip_comment(buf))
            return true;
    }
    return false;
}

// Читает файл input с поддержкой комментариев (!)
//   строка 1: endf_path (обязательно)
//   строка 2: mat (обязательно)
//   строка 3: err (обязательно)
//   строка 4: errmax (опционально, умолчание: 10*err)
//   строка 5: errint (опционально, умолчание: err/20000)
//   строка 6: tempr (опционально, умолчание: 0)
void read_input(const std::string &path, run_config &cfg)
{
    std::ifstream f(path);
    if (!f.is_open())
    {
        fprintf(stderr, "- Error: cannot open input file '%s'\n", path.c_str());
        exit(EXIT_FAILURE);
    }

    std::string buf;

    // строка 1: endf_path
    if (!read_next_nonblank(f, buf))
    {
        fprintf(stderr, "- Error: cannot read endf_path from '%s'\n",
                path.c_str());
        exit(EXIT_FAILURE);
    }
    cfg.endf_path = buf;

    // строка 2: mat
    if (!read_next_nonblank(f, buf))
    {
        fprintf(stderr, "- Error: cannot read endf_mat from '%s'\n",
                path.c_str());
        exit(EXIT_FAILURE);
    }
    if (std::sscanf(buf.c_str(), " %d", &cfg.endf_mat) != 1)
    {
        fprintf(stderr, "- Error: cannot parse endf_mat from '%s'\n",
                buf.c_str());
        exit(EXIT_FAILURE);
    }

    // строка 3: err (обязательно)
    if (!read_next_nonblank(f, buf))
    {
        fprintf(stderr, "- Error: cannot read err from '%s'\n", path.c_str());
        exit(EXIT_FAILURE);
    }
    if (std::sscanf(buf.c_str(), " %lf", &cfg.err) != 1)
    {
        fprintf(stderr, "- Error: cannot parse err from '%s'\n", buf.c_str());
        exit(EXIT_FAILURE);
    }

    // строка 4: errmax (опционально)
    if (read_next_nonblank(f, buf))
    {
        if (std::sscanf(buf.c_str(), " %lf", &cfg.errmax) != 1)
        {
            fprintf(stderr, "- Error: cannot parse errmax from '%s'\n",
                    buf.c_str());
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        cfg.errmax = 10.0 * cfg.err;
    }

    // строка 5: errint (опционально)
    if (read_next_nonblank(f, buf))
    {
        if (std::sscanf(buf.c_str(), " %lf", &cfg.errint) != 1)
        {
            fprintf(stderr, "- Error: cannot parse errint from '%s'\n",
                    buf.c_str());
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        cfg.errint = cfg.err / 20000.0;
    }

    // строка 6: tempr (опционально)
    if (read_next_nonblank(f, buf))
    {
        if (std::sscanf(buf.c_str(), " %lf", &cfg.tempr) != 1)
        {
            fprintf(stderr, "- Error: cannot parse tempr from '%s'\n",
                    buf.c_str());
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        cfg.tempr = 0.0;
    }
}

// Выделяет basename файла, убирая расширение
static std::string extract_basename(const std::string &path)
{
    auto        slash = path.rfind('/');
    std::string base =
        (slash != std::string::npos) ? path.substr(slash + 1) : path;
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
        fprintf(stderr, "- Error: cannot create reconr input '%s'\n", inp_path);
        exit(EXIT_FAILURE);
    }

    std::string label = reconr_label(cfg.endf_path);

    fprintf(f, "reconr\n");
    fprintf(f, " 20 21\n");
    fprintf(f, "'%s'/\n", label.c_str());
    fprintf(f, "%d/\n", cfg.endf_mat);
    fprintf(f, "%g %g %g %g/\n", cfg.err, cfg.tempr, cfg.errmax, cfg.errint);
    fprintf(f, "0/\n");
    fprintf(f, "stop\n");

    fclose(f);
}

// Создаёт symlink tape20 -> endf, запускает njoy < *.inp,
// переименовывает tape21 -> *.reconr, убирает за собой
void run_reconr(run_config &cfg)
{
    std::string inp_path = cfg.endf_name + "-reconr" + ".inp";

    write_reconr_input(cfg, inp_path.c_str());

    unlink("tape20");
    if (symlink(cfg.endf_path.c_str(), "tape20") != 0)
    {
        fprintf(stderr, "- Error: symlink tape20 -> '%s' failed\n",
                cfg.endf_path.c_str());
        exit(EXIT_FAILURE);
    }

    unlink("tape21");

    fprintf(stdout, "+ Running njoy reconr for mat=%d err=%g errmax=%g\n",
            cfg.endf_mat, cfg.err, cfg.errmax);

    std::string cmd = "njoy < " + inp_path;
    int         ret = system(cmd.c_str());
    if (ret != 0)
    {
        fprintf(stderr, "- Error: njoy exited with code %d\n", ret);
        unlink("tape20");
        exit(EXIT_FAILURE);
    }

    if (rename("tape21", cfg.reconr_path.c_str()) != 0)
    {
        fprintf(stderr, "- Error: cannot rename tape21 -> '%s'\n",
                cfg.reconr_path.c_str());
        unlink("tape20");
        exit(EXIT_FAILURE);
    }

    fprintf(stdout, "+ reconr output: %s\n", cfg.reconr_path.c_str());

    // переименовываем output, если существует
    rename("output", cfg.output_path.c_str());

    // убираем symlink
    unlink("tape20");
}

int main()
{
    run_config cfg;
    read_input("input", cfg);

    cfg.endf_dir    = extract_dir(cfg.endf_path);
    cfg.endf_name   = extract_basename(cfg.endf_path);
    cfg.reconr_path = cfg.endf_dir + cfg.endf_name + ".reconr";
    cfg.output_path = cfg.endf_dir + cfg.endf_name + ".output";
    run_reconr(cfg);

    return 0;
}
