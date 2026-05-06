#include <cstdio>
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
    double      errmax;      // точность reconr
};

// Объявления
void read_input(const char *path, run_config &cfg);
void write_reconr_input(const run_config &cfg, const char *inp_path);
void run_reconr(run_config &cfg);

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
    fprintf(f, "%g/\n", cfg.errmax);
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

    fprintf(stdout, "+ Running njoy reconr for mat=%d errmax=%g\n",
            cfg.endf_mat, cfg.errmax);

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
