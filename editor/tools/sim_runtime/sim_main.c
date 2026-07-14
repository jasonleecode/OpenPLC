/* TiZi PC simulation runtime.
 *
 * The generated simulator links matiec output plus a generated sim_vars.c.
 * It speaks a small JSON-lines protocol over stdin/stdout so the editor can
 * run, pause, single-step, inspect and write PLC variables without hardware.
 */
#include "sim_api.h"
#include "iec_std_lib.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

TIME __CURRENT_TIME;
BOOL __DEBUG = 0;

extern void config_init__(void);
extern void config_run__(unsigned long tick);
extern unsigned long long common_ticktime__;

static int g_running = 0;
static unsigned long g_tick = 0;
static unsigned long g_scan_time_us = 0;
static unsigned int g_interval_ms = 10;

static void update_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    __CURRENT_TIME.tv_sec = ts.tv_sec;
    __CURRENT_TIME.tv_nsec = ts.tv_nsec;
}

static unsigned long monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000000UL + (unsigned long)(ts.tv_nsec / 1000L);
}

static void json_escape(const char *text)
{
    putchar('"');
    for (const unsigned char *p = (const unsigned char *)text; *p; ++p) {
        if (*p == '"' || *p == '\\') {
            putchar('\\');
            putchar(*p);
        } else if (*p == '\n') {
            fputs("\\n", stdout);
        } else if (*p == '\r') {
            fputs("\\r", stdout);
        } else if (*p == '\t') {
            fputs("\\t", stdout);
        } else if (*p >= 0x20) {
            putchar(*p);
        }
    }
    putchar('"');
}

static const char *type_name(SimVarType type)
{
    switch (type) {
    case SIM_VAR_BOOL: return "BOOL";
    case SIM_VAR_INT: return "INT";
    case SIM_VAR_DINT: return "DINT";
    case SIM_VAR_REAL: return "REAL";
    case SIM_VAR_LREAL: return "LREAL";
    }
    return "UNKNOWN";
}

static SimVar *find_var(const char *name)
{
    for (size_t i = 0; i < sim_var_count; ++i) {
        if (strcmp(sim_vars[i].name, name) == 0)
            return &sim_vars[i];
    }
    return NULL;
}

static double read_number(const SimVar *var)
{
    switch (var->type) {
    case SIM_VAR_BOOL: return *(IEC_BOOL *)var->value ? 1.0 : 0.0;
    case SIM_VAR_INT: return (double)*(IEC_INT *)var->value;
    case SIM_VAR_DINT: return (double)*(IEC_DINT *)var->value;
    case SIM_VAR_REAL: return (double)*(IEC_REAL *)var->value;
    case SIM_VAR_LREAL: return *(IEC_LREAL *)var->value;
    }
    return 0.0;
}

static void write_number(SimVar *var, double value)
{
    switch (var->type) {
    case SIM_VAR_BOOL:
        *(IEC_BOOL *)var->value = fabs(value) > 0.000001 ? 1 : 0;
        break;
    case SIM_VAR_INT:
        *(IEC_INT *)var->value = (IEC_INT)value;
        break;
    case SIM_VAR_DINT:
        *(IEC_DINT *)var->value = (IEC_DINT)value;
        break;
    case SIM_VAR_REAL:
        *(IEC_REAL *)var->value = (IEC_REAL)value;
        break;
    case SIM_VAR_LREAL:
        *(IEC_LREAL *)var->value = (IEC_LREAL)value;
        break;
    }
}

static void apply_forces(void)
{
    for (size_t i = 0; i < sim_var_count; ++i) {
        if (sim_vars[i].forced)
            write_number(&sim_vars[i], sim_vars[i].forced_number);
    }
}

static void run_scan(void)
{
    apply_forces();
    update_time();
    unsigned long start = monotonic_us();
    config_run__(g_tick++);
    unsigned long end = monotonic_us();
    g_scan_time_us = end >= start ? end - start : 0;
    apply_forces();
}

static void print_value(const SimVar *var)
{
    if (var->type == SIM_VAR_BOOL) {
        fputs(*(IEC_BOOL *)var->value ? "true" : "false", stdout);
    } else if (var->type == SIM_VAR_REAL || var->type == SIM_VAR_LREAL) {
        printf("%.9g", read_number(var));
    } else {
        printf("%lld", (long long)read_number(var));
    }
}

static void print_var(const SimVar *var)
{
    fputs("{\"name\":", stdout);
    json_escape(var->name);
    fputs(",\"type\":", stdout);
    json_escape(type_name(var->type));
    fputs(",\"value\":", stdout);
    print_value(var);
    printf(",\"forced\":%s}", var->forced ? "true" : "false");
}

static void print_status(void)
{
    printf("{\"ok\":true,\"running\":%s,\"tick\":%lu,\"scanTimeUs\":%lu,\"intervalMs\":%u}\n",
           g_running ? "true" : "false",
           g_tick,
           g_scan_time_us,
           g_interval_ms);
    fflush(stdout);
}

static void print_error(const char *message)
{
    fputs("{\"ok\":false,\"error\":", stdout);
    json_escape(message);
    fputs("}\n", stdout);
    fflush(stdout);
}

static int extract_string(const char *line, const char *key, char *out, size_t out_size)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(line, pattern);
    if (!p) return 0;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return 0;
    p++;
    while (isspace((unsigned char)*p)) p++;
    if (*p != '"') return 0;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < out_size) {
        if (*p == '\\' && p[1])
            p++;
        out[n++] = *p++;
    }
    out[n] = '\0';
    return n > 0 || (*p == '"');
}

static int extract_number(const char *line, const char *key, double *out)
{
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(line, pattern);
    if (!p) return 0;
    p = strchr(p + strlen(pattern), ':');
    if (!p) return 0;
    p++;
    while (isspace((unsigned char)*p)) p++;
    if (strncmp(p, "true", 4) == 0) {
        *out = 1.0;
        return 1;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = 0.0;
        return 1;
    }
    char *end = NULL;
    errno = 0;
    double value = strtod(p, &end);
    if (errno != 0 || end == p)
        return 0;
    *out = value;
    return 1;
}

static int command_is(const char *line, const char *cmd)
{
    char parsed[64];
    return extract_string(line, "cmd", parsed, sizeof(parsed)) && strcmp(parsed, cmd) == 0;
}

static void handle_read_vars(const char *line)
{
    char name[160];
    fputs("{\"ok\":true,\"vars\":[", stdout);
    if (extract_string(line, "name", name, sizeof(name))) {
        SimVar *var = find_var(name);
        if (!var) {
            fputs("]}\n", stdout);
            fflush(stdout);
            return;
        }
        print_var(var);
    } else {
        for (size_t i = 0; i < sim_var_count; ++i) {
            if (i) putchar(',');
            print_var(&sim_vars[i]);
        }
    }
    fputs("]}\n", stdout);
    fflush(stdout);
}

static void handle_set_trace_variables(const char *line)
{
    for (size_t i = 0; i < sim_var_count; ++i)
        sim_vars[i].traced = 0;

    const char *names = strstr(line, "\"names\"");
    if (names) {
        const char *p = strchr(names, '[');
        const char *end = p ? strchr(p, ']') : NULL;
        while (p && end && p < end) {
            while (p < end && *p != '"') p++;
            if (p >= end) break;
            p++;

            char name[160];
            size_t n = 0;
            while (p < end && *p && *p != '"' && n + 1 < sizeof(name)) {
                if (*p == '\\' && p[1])
                    p++;
                name[n++] = *p++;
            }
            name[n] = '\0';

            SimVar *var = find_var(name);
            if (var)
                var->traced = 1;
            while (p < end && *p != ',') p++;
        }
    }

    fputs("{\"ok\":true}\n", stdout);
    fflush(stdout);
}

static void handle_trace_data(void)
{
    fputs("{\"ok\":true,\"tick\":", stdout);
    printf("%lu", g_tick);
    fputs(",\"vars\":[", stdout);

    int first = 1;
    for (size_t i = 0; i < sim_var_count; ++i) {
        if (!sim_vars[i].traced)
            continue;
        if (!first) putchar(',');
        print_var(&sim_vars[i]);
        first = 0;
    }

    fputs("]}\n", stdout);
    fflush(stdout);
}

static void handle_write_var(const char *line, int force)
{
    char name[160];
    double value = 0.0;
    if (!extract_string(line, "name", name, sizeof(name)) || !extract_number(line, "value", &value)) {
        print_error("writeVar requires name and value");
        return;
    }

    SimVar *var = find_var(name);
    if (!var) {
        print_error("unknown variable");
        return;
    }

    if (force) {
        var->forced = 1;
        var->forced_number = value;
    }
    write_number(var, value);
    fputs("{\"ok\":true}\n", stdout);
    fflush(stdout);
}

static void handle_release_force(const char *line)
{
    char name[160];
    if (!extract_string(line, "name", name, sizeof(name))) {
        print_error("releaseForce requires name");
        return;
    }
    SimVar *var = find_var(name);
    if (!var) {
        print_error("unknown variable");
        return;
    }
    var->forced = 0;
    fputs("{\"ok\":true}\n", stdout);
    fflush(stdout);
}

static int handle_command(char *line)
{
    if (command_is(line, "hello")) {
        printf("{\"ok\":true,\"name\":\"TiZi SmartSim\",\"version\":1,\"varCount\":%zu}\n", sim_var_count);
        fflush(stdout);
    } else if (command_is(line, "init") || command_is(line, "reset")) {
        config_init__();
        g_tick = 0;
        g_scan_time_us = 0;
        g_running = 0;
        fputs("{\"ok\":true}\n", stdout);
        fflush(stdout);
    } else if (command_is(line, "start")) {
        double interval = 0.0;
        if (extract_number(line, "intervalMs", &interval) && interval > 0.0)
            g_interval_ms = (unsigned int)interval;
        g_running = 1;
        print_status();
    } else if (command_is(line, "pause")) {
        g_running = 0;
        print_status();
    } else if (command_is(line, "stop")) {
        g_running = 0;
        return 0;
    } else if (command_is(line, "step")) {
        g_running = 0;
        run_scan();
        print_status();
    } else if (command_is(line, "status")) {
        print_status();
    } else if (command_is(line, "readVars")) {
        handle_read_vars(line);
    } else if (command_is(line, "setTraceVariables")) {
        handle_set_trace_variables(line);
    } else if (command_is(line, "traceData")) {
        handle_trace_data();
    } else if (command_is(line, "writeVar")) {
        handle_write_var(line, 0);
    } else if (command_is(line, "forceVar")) {
        handle_write_var(line, 1);
    } else if (command_is(line, "releaseForce")) {
        handle_release_force(line);
    } else {
        print_error("unknown command");
    }
    return 1;
}

int main(void)
{
    config_init__();
    unsigned int default_ms = (unsigned int)(common_ticktime__ / 1000000ULL);
    if (default_ms > 0)
        g_interval_ms = default_ms;

    char line[4096];
    int alive = 1;
    while (alive) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);

        struct timeval tv;
        tv.tv_sec = g_running ? (time_t)(g_interval_ms / 1000U) : 1;
        tv.tv_usec = g_running ? (suseconds_t)((g_interval_ms % 1000U) * 1000U) : 0;

        int ready = select(STDIN_FILENO + 1, &rfds, NULL, NULL, &tv);
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            if (!fgets(line, sizeof(line), stdin))
                break;
            alive = handle_command(line);
        } else if (ready == 0 && g_running) {
            run_scan();
        }
    }

    fputs("{\"ok\":true,\"stopped\":true}\n", stdout);
    fflush(stdout);
    return 0;
}
