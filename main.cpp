#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#include "endf_parser.h"

// ---------------------------------------------------------------------------
// Конфигурация запуска
// ---------------------------------------------------------------------------

const int16_t PATH_LEN = 1024;

struct run_config
{
    std::string endf_path;   // путь к исходному endf-файлу
    std::string endf_dir;
    std::string endf_name;
    std::string reconr_path;
    std::string output_path;
    int32_t     endf_mat;    // номер материала
    double      err;         // точность сечения (обязательно)
    double      errmax;      // макс ошибка (умолчание: 10*err)
    double      errint;      // интерполяция ошибки (умолчание: err/20000)
    double      tempr;       // температура (умолчание: 0)
};

// Объявления
void read_input(const char *path, run_config &cfg);
void write_reconr_input(const run_config &cfg, const char *inp_path);
void run_reconr(run_config &cfg);

static std::string extract_basename(const std::string &path);
static std::string extract_dir(const std::string &path);
static void strip_comment(char *buf);
static bool is_blank(const char *buf);
static int read_next_nonblank(FILE *f, char *buf, int maxlen);

// Удаляет inline комментарий (# или !) и завершающий перевод строки
static void strip_comment(char *buf)
{
    for (int i = 0; buf[i] != '\0'; ++i)
    {
        if (buf[i] == '#' || buf[i] == '!')
        {
            buf[i] = '\0';
            break;
        }
    }
    // убираем trailing whitespace/newline
    int len = (int)std::strlen(buf);
    while (len > 0 && (buf[len - 1] == ' ' || buf[len - 1] == '\t' ||
                       buf[len - 1] == '\n' || buf[len - 1] == '\r'))
    {
        buf[--len] = '\0';
    }
}

// Проверяет, что строка пустая или только пробелы
static bool is_blank(const char *buf)
{
    for (int i = 0; buf[i] != '\0'; ++i)
    {
        if (buf[i] != ' ' && buf[i] != '\t' && buf[i] != '\n' &&
            buf[i] != '\r')
        {
            return false;
        }
    }
    return true;
}

// Читает следующую непустую (после strip_comment) строку.
// Возвращает 0 при EOF, 1 при успехе.
static int read_next_nonblank(FILE *f, char *buf, int maxlen)
{
    while (std::fgets(buf, maxlen, f) != NULL)
    {
        strip_comment(buf);
        if (!is_blank(buf))
            return 1;
    }
    return 0;
}

// Читает файл input с поддержкой комментариев (#, !)
//   строка 1: endf_path (обязательно)
//   строка 2: mat (обязательно)
//   строка 3: err (обязательно)
//   строка 4: errmax (опционально, умолчание: 10*err)
//   строка 5: errint (опционально, умолчание: err/20000)
//   строка 6: tempr (опционально, умолчание: 0)
void read_input(const char *path, run_config &cfg)
{
    FILE *f = fopen(path, "r");
    if (f == NULL)
    {
        fprintf(stderr, "- Error: cannot open input file '%s'\n", path);
        exit(EXIT_FAILURE);
    }

    char buf[PATH_LEN];

    // строка 1: endf_path
    if (read_next_nonblank(f, buf, PATH_LEN) == 0)
    {
        fprintf(stderr, "- Error: cannot read endf_path from '%s'\n", path);
        fclose(f);
        exit(EXIT_FAILURE);
    }
    cfg.endf_path = buf;

    // строка 2: mat
    if (read_next_nonblank(f, buf, PATH_LEN) == 0)
    {
        fprintf(stderr, "- Error: cannot read endf_mat from '%s'\n", path);
        fclose(f);
        exit(EXIT_FAILURE);
    }
    if (std::sscanf(buf, " %d", &cfg.endf_mat) != 1)
    {
        fprintf(stderr, "- Error: cannot parse endf_mat from '%s'\n", buf);
        fclose(f);
        exit(EXIT_FAILURE);
    }

    // строка 3: err (обязательно)
    if (read_next_nonblank(f, buf, PATH_LEN) == 0)
    {
        fprintf(stderr, "- Error: cannot read err from '%s'\n", path);
        fclose(f);
        exit(EXIT_FAILURE);
    }
    if (std::sscanf(buf, " %lf", &cfg.err) != 1)
    {
        fprintf(stderr, "- Error: cannot parse err from '%s'\n", buf);
        fclose(f);
        exit(EXIT_FAILURE);
    }

    // строка 4: errmax (опционально)
    if (read_next_nonblank(f, buf, PATH_LEN) == 1)
    {
        if (std::sscanf(buf, " %lf", &cfg.errmax) != 1)
        {
            fprintf(stderr, "- Error: cannot parse errmax from '%s'\n", buf);
            fclose(f);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        cfg.errmax = 10.0 * cfg.err;
    }

    // строка 5: errint (опционально)
    if (read_next_nonblank(f, buf, PATH_LEN) == 1)
    {
        if (std::sscanf(buf, " %lf", &cfg.errint) != 1)
        {
            fprintf(stderr, "- Error: cannot parse errint from '%s'\n", buf);
            fclose(f);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        cfg.errint = cfg.err / 20000.0;
    }

    // строка 6: tempr (опционально)
    if (read_next_nonblank(f, buf, PATH_LEN) == 1)
    {
        if (std::sscanf(buf, " %lf", &cfg.tempr) != 1)
        {
            fprintf(stderr, "- Error: cannot parse tempr from '%s'\n", buf);
            fclose(f);
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        cfg.tempr = 0.0;
    }

    fclose(f);
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
    fprintf(f, "%g/\n", cfg.err);
    fprintf(f, "%g %g %g %g/\n", cfg.err, cfg.tempr, cfg.errmax, cfg.errint);
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
