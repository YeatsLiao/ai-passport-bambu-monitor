#include "prov_form.h"
#include <string.h>
#include <stdlib.h>

static int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// -1 malformed/oversized/duplicate; 0 absent; 1 present (including empty).
static int field(const char *body, const char *key, char *out, size_t size) {
    int found = 0;
    size_t n = strlen(key);
    for (const char *p = body; *p;) {
        const char *end = strchr(p, '&');
        if (!end) end = p + strlen(p);
        if ((size_t)(end - p) > n && !memcmp(p, key, n) && p[n] == '=') {
            if (found) return -1;
            found = 1;
            size_t used = 0;
            for (p += n + 1; p < end; ++p) {
                unsigned char c = *p;
                if (c == '+') c = ' ';
                else if (c == '%') {
                    if (end - p < 3 || hex(p[1]) < 0 || hex(p[2]) < 0) return -1;
                    c = hex(p[1]) * 16 + hex(p[2]);
                    p += 2;
                }
                if (!c || c < 32 || c == 127 || used + 1 >= size) return -1;
                out[used++] = c;
            }
            out[used] = 0;
        }
        p = *end ? end + 1 : end;
    }
    return found;
}

static bool ipv4(const char *p) {
    for (int i = 0; i < 4; ++i) {
        int v = 0, digits = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + *p++ - '0'; ++digits; if (digits > 3) return false; }
        if (!digits || v > 255 || (i < 3 ? *p != '.' : *p != 0)) return false;
        if (i < 3) ++p;
    }
    return true;
}

const char *prov_form_parse(const char *body, const app_config_t *old,
                            app_config_t *out, bool *force) {
    *out = *old;
    *force = false;
    char pass[sizeof(out->wifi_pass)] = "", code[sizeof(out->access_code)] = "";
    char style[4] = "", lang[4] = "", order[80] = "", fv[4] = "";
    if (field(body, "ssid", out->wifi_ssid, sizeof(out->wifi_ssid)) < 0 ||
        field(body, "pass", pass, sizeof(pass)) < 0 ||
        field(body, "ip", out->printer_ip, sizeof(out->printer_ip)) < 0 ||
        field(body, "serial", out->printer_serial, sizeof(out->printer_serial)) < 0 ||
        field(body, "code", code, sizeof(code)) < 0 ||
        field(body, "style", style, sizeof(style)) < 0 ||
        field(body, "lang", lang, sizeof(lang)) < 0 ||
        field(body, "force", fv, sizeof(fv)) < 0)
        return "表单字段过长或编码错误，请重新填写";
    int has_order = field(body, "order", order, sizeof(order));
    if (has_order < 0) return "组件顺序格式错误";
    if (!out->wifi_ssid[0]) return "WiFi 名称不能为空";
    if (!pass[0] && strcmp(out->wifi_ssid, old->wifi_ssid)) return "更换 WiFi 时请填写新密码";
    if (pass[0]) strcpy(out->wifi_pass, pass);
    if (!code[0] && strcmp(out->printer_serial, old->printer_serial)) return "更换打印机时请填写新访问码";
    if (code[0]) strcpy(out->access_code, code);
    if (!ipv4(out->printer_ip)) return "打印机 IP 格式错误";
    if (!out->printer_serial[0]) return "打印机序列号不能为空";
    for (const char *p = out->printer_serial; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z')))
            return "序列号只能包含英文字母和数字";
    if (strlen(out->access_code) != 8) return "访问码应为 8 位";
    if (style[0]) {
        char *end; long n = strtol(style, &end, 10);
        if (*end || n < 1 || n > 12) return "界面风格无效";
        out->ui_style = n;
    }
    if (lang[0]) {
        if (strcmp(lang, "1") && strcmp(lang, "2")) return "界面语言无效";
        out->lang = lang[0] - '0';
    }
    if (has_order) app_config_set_comp_order_csv(out, order);
    *force = !strcmp(fv, "1");
    return NULL;
}
