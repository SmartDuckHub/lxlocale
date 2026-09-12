/*
 * lxlocale - een klein GTK+3 configuratieprogramma voor taal (locale) en
 * toetsenbordindeling, in de geest van lxinput.
 *
 * Bouwen:  make
 * Vereist: gtk+-3.0, setxkbmap (x11-xkb-utils / x11-xserver-utils)
 *
 * Licentie: BSD 3-Clause
 */

#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <string.h>
#include <stdlib.h>

#define BLOCK_BEGIN "# >>> lxlocale begin >>>"
#define BLOCK_END   "# <<< lxlocale end <<<"
#define ENVD_FILE   "60-lxlocale.conf"

/* ------------------------------------------------------------------ */
/* datastructuren                                                      */
/* ------------------------------------------------------------------ */

typedef struct {
    gchar *code;    /* bv. "nl", "pc105", "sundeadkeys", "grp:alt_shift_toggle" */
    gchar *desc;    /* omschrijving uit de xkb-rules                            */
    gchar *parent;  /* alleen bij varianten: de indeling waar hij bij hoort     */
} XkbItem;

typedef struct {
    GtkWidget *window;

    /* tab Taal */
    GtkWidget *lang_combo;
    GtkWidget *region_combo;

    /* tab Toetsenbord */
    GtkWidget *model_combo;
    GtkWidget *layout_combo;
    GtkWidget *variant_combo;
    GtkWidget *layout2_combo;
    GtkWidget *variant2_combo;
    GtkWidget *switch_combo;
    GtkWidget *test_entry;

    /* tab Opslaan */
    GtkWidget *chk_profile;
    GtkWidget *chk_xprofile;
    GtkWidget *chk_xinitrc;
    GtkWidget *chk_fluxbox;
    GtkWidget *chk_envd;
    GtkWidget *chk_system;

    /* xkb-instellingen zoals ze waren bij het starten */
    gchar *orig_model;
    gchar *orig_layout;
    gchar *orig_variant;
    gchar *orig_option;

    gboolean loading;   /* onderdruk signalen tijdens het vullen van combo's */
    gboolean applied;   /* is er toegepast? zo niet: xkb terugdraaien        */
} App;

static GPtrArray *xkb_models;
static GPtrArray *xkb_layouts;
static GPtrArray *xkb_variants;
static GPtrArray *xkb_options;

/* ------------------------------------------------------------------ */
/* hulpfuncties                                                        */
/* ------------------------------------------------------------------ */

static void xkb_item_free(gpointer p)
{
    XkbItem *it = p;
    if (!it)
        return;
    g_free(it->code);
    g_free(it->desc);
    g_free(it->parent);
    g_free(it);
}

static gint xkb_item_cmp(gconstpointer a, gconstpointer b)
{
    const XkbItem *x = *(XkbItem * const *) a;
    const XkbItem *y = *(XkbItem * const *) b;
    return g_utf8_collate(x->desc, y->desc);
}

/* g_ptr_array_sort geeft een pointer naar het element mee, dus gchar** */
static gint strp_cmp(gconstpointer a, gconstpointer b)
{
    return g_strcmp0(*(const gchar * const *) a, *(const gchar * const *) b);
}

static gchar *run_capture(const gchar *cmdline)
{
    gchar *out = NULL;
    GError *err = NULL;

    if (!g_spawn_command_line_sync(cmdline, &out, NULL, NULL, &err)) {
        if (err)
            g_error_free(err);
        return NULL;
    }
    return out;
}

static gboolean run_argv(gchar **argv)
{
    GError *err = NULL;
    gint status = 0;

    if (!g_spawn_sync(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
                      NULL, NULL, NULL, NULL, &status, &err)) {
        if (err) {
            g_warning("%s", err->message);
            g_error_free(err);
        }
        return FALSE;
    }
    return status == 0;
}

static gboolean have_program(const gchar *name)
{
    gchar *path = g_find_program_in_path(name);
    gboolean found = (path != NULL);
    g_free(path);
    return found;
}

/* ------------------------------------------------------------------ */
/* xkb: regels inlezen, opvragen en toepassen                          */
/* ------------------------------------------------------------------ */

static void load_xkb_rules(void)
{
    static const gchar *candidates[] = {
        "/usr/share/X11/xkb/rules/evdev.lst",
        "/usr/share/X11/xkb/rules/base.lst",
        "/usr/local/share/X11/xkb/rules/evdev.lst",
        NULL
    };
    gchar *content = NULL;
    gchar **lines;
    GPtrArray *cur = NULL;
    guint i;

    xkb_models   = g_ptr_array_new_with_free_func(xkb_item_free);
    xkb_layouts  = g_ptr_array_new_with_free_func(xkb_item_free);
    xkb_variants = g_ptr_array_new_with_free_func(xkb_item_free);
    xkb_options  = g_ptr_array_new_with_free_func(xkb_item_free);

    for (i = 0; candidates[i]; i++)
        if (g_file_get_contents(candidates[i], &content, NULL, NULL))
            break;
    if (!content)
        return;

    lines = g_strsplit(content, "\n", -1);
    for (i = 0; lines[i]; i++) {
        gchar *s, *sep, *desc;
        XkbItem *it;

        if (lines[i][0] == '!') {
            if (g_str_has_prefix(lines[i], "! model"))
                cur = xkb_models;
            else if (g_str_has_prefix(lines[i], "! layout"))
                cur = xkb_layouts;
            else if (g_str_has_prefix(lines[i], "! variant"))
                cur = xkb_variants;
            else if (g_str_has_prefix(lines[i], "! option"))
                cur = xkb_options;
            else
                cur = NULL;
            continue;
        }
        if (!cur)
            continue;

        s = g_strdup(lines[i]);
        g_strstrip(s);
        if (!*s) {
            g_free(s);
            continue;
        }
        sep = s;
        while (*sep && !g_ascii_isspace(*sep))
            sep++;
        if (!*sep) {            /* regel zonder omschrijving: overslaan */
            g_free(s);
            continue;
        }
        *sep++ = '\0';
        desc = g_strstrip(sep);

        it = g_new0(XkbItem, 1);
        it->code = g_strdup(s);
        if (cur == xkb_variants) {
            gchar *colon = strchr(desc, ':');
            if (colon) {
                *colon = '\0';
                it->parent = g_strdup(g_strstrip(desc));
                desc = g_strstrip(colon + 1);
            }
        }
        it->desc = g_strdup(desc);
        g_ptr_array_add(cur, it);
        g_free(s);
    }
    g_strfreev(lines);
    g_free(content);

    g_ptr_array_sort(xkb_models,  xkb_item_cmp);
    g_ptr_array_sort(xkb_layouts, xkb_item_cmp);
    g_ptr_array_sort(xkb_options, xkb_item_cmp);
}

static void xkb_query(gchar **model, gchar **layout, gchar **variant, gchar **option)
{
    gchar *out, **lines;
    guint i;

    *model = *layout = *variant = *option = NULL;
    out = run_capture("setxkbmap -query");
    if (!out)
        return;

    lines = g_strsplit(out, "\n", -1);
    for (i = 0; lines[i]; i++) {
        gchar *colon = strchr(lines[i], ':');
        gchar *key, *val;

        if (!colon)
            continue;
        *colon = '\0';
        key = g_strstrip(lines[i]);
        val = g_strstrip(colon + 1);

        if (!g_strcmp0(key, "model"))
            *model = g_strdup(val);
        else if (!g_strcmp0(key, "layout"))
            *layout = g_strdup(val);
        else if (!g_strcmp0(key, "variant"))
            *variant = g_strdup(val);
        else if (!g_strcmp0(key, "options"))
            *option = g_strdup(val);
    }
    g_strfreev(lines);
    g_free(out);
}

static void xkb_apply(const gchar *model, const gchar *layout,
                      const gchar *variant, const gchar *option)
{
    GPtrArray *a = g_ptr_array_new();

    g_ptr_array_add(a, "setxkbmap");
    if (model && *model) {
        g_ptr_array_add(a, "-model");
        g_ptr_array_add(a, (gpointer) model);
    }
    if (layout && *layout) {
        g_ptr_array_add(a, "-layout");
        g_ptr_array_add(a, (gpointer) layout);
    }
    if (variant && *variant) {
        g_ptr_array_add(a, "-variant");
        g_ptr_array_add(a, (gpointer) variant);
    }
    /* eerst alle bestaande opties wissen, daarna eventueel eentje zetten */
    g_ptr_array_add(a, "-option");
    g_ptr_array_add(a, "");
    if (option && *option) {
        g_ptr_array_add(a, "-option");
        g_ptr_array_add(a, (gpointer) option);
    }
    g_ptr_array_add(a, NULL);

    run_argv((gchar **) a->pdata);
    g_ptr_array_free(a, TRUE);
}

/* ------------------------------------------------------------------ */
/* locales                                                             */
/* ------------------------------------------------------------------ */

static const struct { const gchar *code; const gchar *name; } LANGNAMES[] = {
    { "af", "Afrikaans" },      { "ar", "Arabisch" },       { "be", "Wit-Russisch" },
    { "bg", "Bulgaars" },       { "ca", "Catalaans" },      { "cs", "Tsjechisch" },
    { "da", "Deens" },          { "de", "Duits" },          { "el", "Grieks" },
    { "en", "Engels" },         { "es", "Spaans" },         { "et", "Estisch" },
    { "eu", "Baskisch" },       { "fa", "Perzisch" },       { "fi", "Fins" },
    { "fr", "Frans" },          { "ga", "Iers" },           { "he", "Hebreeuws" },
    { "hi", "Hindi" },          { "hr", "Kroatisch" },      { "hu", "Hongaars" },
    { "id", "Indonesisch" },    { "is", "IJslands" },       { "it", "Italiaans" },
    { "ja", "Japans" },         { "ko", "Koreaans" },       { "lt", "Litouws" },
    { "lv", "Lets" },           { "nb", "Noors (Bokmal)" }, { "nl", "Nederlands" },
    { "nn", "Noors (Nynorsk)" },{ "pl", "Pools" },          { "pt", "Portugees" },
    { "ro", "Roemeens" },       { "ru", "Russisch" },       { "sk", "Slowaaks" },
    { "sl", "Sloveens" },       { "sr", "Servisch" },       { "sv", "Zweeds" },
    { "th", "Thais" },          { "tr", "Turks" },          { "uk", "Oekraiens" },
    { "vi", "Vietnamees" },     { "zh", "Chinees" },        { NULL, NULL }
};

static gchar *locale_label(const gchar *locale)
{
    gchar lang[3] = { 0, 0, 0 };
    guint i;

    if (!g_ascii_isalpha(locale[0]))
        return g_strdup(locale);
    lang[0] = g_ascii_tolower(locale[0]);
    lang[1] = g_ascii_isalpha(locale[1]) ? g_ascii_tolower(locale[1]) : '\0';

    for (i = 0; LANGNAMES[i].code; i++)
        if (!g_strcmp0(LANGNAMES[i].code, lang))
            return g_strdup_printf("%s  (%s)", LANGNAMES[i].name, locale);

    return g_strdup(locale);
}

/* "nl_NL.utf8" -> "nl_NL.UTF-8" */
static gchar *normalize_locale(const gchar *s)
{
    gchar *dot = strchr(s, '.');

    if (dot && (!g_ascii_strcasecmp(dot + 1, "utf8") ||
                !g_ascii_strcasecmp(dot + 1, "UTF-8"))) {
        gchar *base = g_strndup(s, dot - s);
        gchar *res = g_strconcat(base, ".UTF-8", NULL);
        g_free(base);
        return res;
    }
    return g_strdup(s);
}

static GPtrArray *load_locales(void)
{
    GPtrArray *arr = g_ptr_array_new_with_free_func(g_free);
    GHashTable *seen = g_hash_table_new(g_str_hash, g_str_equal);
    gchar *out = run_capture("locale -a");

    if (out) {
        gchar **lines = g_strsplit(out, "\n", -1);
        guint i;

        for (i = 0; lines[i]; i++) {
            gchar *l = g_strstrip(lines[i]);
            gchar *norm;

            if (!*l)
                continue;
            /* alleen bruikbare taal-locales: met tekenset, of C/POSIX */
            if (!strchr(l, '.') && g_strcmp0(l, "C") && g_strcmp0(l, "POSIX"))
                continue;
            norm = normalize_locale(l);
            if (g_hash_table_contains(seen, norm)) {
                g_free(norm);
                continue;
            }
            g_hash_table_add(seen, norm);
            g_ptr_array_add(arr, norm);
        }
        g_strfreev(lines);
        g_free(out);
    }
    if (arr->len == 0) {
        g_ptr_array_add(arr, g_strdup("C.UTF-8"));
        g_ptr_array_add(arr, g_strdup("en_US.UTF-8"));
    }
    g_ptr_array_sort(arr, strp_cmp);
    g_hash_table_destroy(seen);
    return arr;
}

/* ------------------------------------------------------------------ */
/* wegschrijven van instellingen                                       */
/* ------------------------------------------------------------------ */

/*
 * Vervangt (of plaatst) het lxlocale-blok in een shellbestand.
 * before_exec: plaats het blok voor de eerste "exec"-regel, zodat het
 * in ~/.xinitrc of ~/.fluxbox/startup nog uitgevoerd wordt.
 */
static gboolean write_block(const gchar *path, const gchar *body,
                            gboolean before_exec, GError **error)
{
    gchar *dir = g_path_get_dirname(path);
    gchar *old = NULL;
    gchar **lines = NULL;
    GPtrArray *kept = g_ptr_array_new();
    GString *out = g_string_new(NULL);
    guint insert, i;
    gboolean ok;

    g_mkdir_with_parents(dir, 0755);
    g_free(dir);

    if (g_file_get_contents(path, &old, NULL, NULL)) {
        gboolean skip = FALSE;

        lines = g_strsplit(old, "\n", -1);
        for (i = 0; lines[i]; i++) {
            if (strstr(lines[i], "lxlocale begin")) {
                skip = TRUE;
                continue;
            }
            if (strstr(lines[i], "lxlocale end")) {
                skip = FALSE;
                continue;
            }
            if (skip)
                continue;
            /* voorkom dat er bij elke ronde lege regels bijkomen */
            if (!*lines[i] && kept->len > 0 &&
                !*((gchar *) g_ptr_array_index(kept, kept->len - 1)))
                continue;
            g_ptr_array_add(kept, lines[i]);
        }
        while (kept->len > 0 &&
               *((gchar *) g_ptr_array_index(kept, kept->len - 1)) == '\0')
            g_ptr_array_remove_index(kept, kept->len - 1);
    }

    insert = kept->len;
    if (before_exec) {
        for (i = 0; i < kept->len; i++) {
            const gchar *l = g_ptr_array_index(kept, i);
            while (*l == ' ' || *l == '\t')
                l++;
            if (g_str_has_prefix(l, "exec ") || !g_strcmp0(l, "exec")) {
                insert = i;
                break;
            }
        }
    }

    for (i = 0; i < kept->len; i++) {
        if (i == insert) {
            g_string_append(out, BLOCK_BEGIN "\n");
            g_string_append(out, body);
            g_string_append(out, BLOCK_END "\n\n");
        }
        g_string_append(out, g_ptr_array_index(kept, i));
        g_string_append_c(out, '\n');
    }
    if (insert >= kept->len) {
        if (kept->len > 0)
            g_string_append_c(out, '\n');
        g_string_append(out, BLOCK_BEGIN "\n");
        g_string_append(out, body);
        g_string_append(out, BLOCK_END "\n");
    }

    ok = g_file_set_contents(path, out->str, -1, error);

    g_string_free(out, TRUE);
    g_ptr_array_free(kept, TRUE);
    if (lines)
        g_strfreev(lines);
    g_free(old);
    return ok;
}

/* ------------------------------------------------------------------ */
/* huidige keuzes uitlezen                                             */
/* ------------------------------------------------------------------ */

static const gchar *combo_id(GtkWidget *c)
{
    const gchar *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(c));
    return id ? id : "";
}

/* bouwt "nl" of "nl,us" */
static gchar *current_layout_string(App *app)
{
    const gchar *l1 = combo_id(app->layout_combo);
    const gchar *l2 = combo_id(app->layout2_combo);

    if (*l2)
        return g_strdup_printf("%s,%s", l1, l2);
    return g_strdup(l1);
}

/* bouwt "" of "intl," of "intl,dvorak" */
static gchar *current_variant_string(App *app)
{
    const gchar *v1 = combo_id(app->variant_combo);
    const gchar *v2 = combo_id(app->variant2_combo);
    const gchar *l2 = combo_id(app->layout2_combo);

    if (*l2)
        return g_strdup_printf("%s,%s", v1, v2);
    return g_strdup(v1);
}

static void apply_xkb_now(App *app)
{
    gchar *layout, *variant;

    if (app->loading)
        return;
    layout = current_layout_string(app);
    variant = current_variant_string(app);
    xkb_apply(combo_id(app->model_combo), layout, variant,
              combo_id(app->switch_combo));
    g_free(layout);
    g_free(variant);
}

static gchar *build_setxkbmap_line(App *app)
{
    gchar *layout = current_layout_string(app);
    gchar *variant = current_variant_string(app);
    const gchar *model = combo_id(app->model_combo);
    const gchar *option = combo_id(app->switch_combo);
    GString *s = g_string_new("setxkbmap");
    gchar *q;

    if (*model) {
        q = g_shell_quote(model);
        g_string_append_printf(s, " -model %s", q);
        g_free(q);
    }
    if (*layout) {
        q = g_shell_quote(layout);
        g_string_append_printf(s, " -layout %s", q);
        g_free(q);
    }
    if (*variant && strcmp(variant, ",")) {
        q = g_shell_quote(variant);
        g_string_append_printf(s, " -variant %s", q);
        g_free(q);
    }
    g_string_append(s, " -option ''");
    if (*option) {
        q = g_shell_quote(option);
        g_string_append_printf(s, " -option %s", q);
        g_free(q);
    }
    g_free(layout);
    g_free(variant);
    return g_string_free(s, FALSE);
}

static const gchar *REGION_VARS[] = {
    "LC_TIME", "LC_NUMERIC", "LC_MONETARY", "LC_PAPER",
    "LC_MEASUREMENT", "LC_ADDRESS", "LC_TELEPHONE", "LC_NAME",
    "LC_IDENTIFICATION", NULL
};

/* shell-versie: export-regels, optioneel met de setxkbmap-regel erbij */
static gchar *build_shell_body(App *app, gboolean with_keyboard)
{
    const gchar *lang = combo_id(app->lang_combo);
    const gchar *region = combo_id(app->region_combo);
    GString *s = g_string_new("");
    guint i;

    if (*lang)
        g_string_append_printf(s, "export LANG='%s'\n", lang);
    if (*region && g_strcmp0(region, lang))
        for (i = 0; REGION_VARS[i]; i++)
            g_string_append_printf(s, "export %s='%s'\n", REGION_VARS[i], region);

    if (with_keyboard) {
        gchar *cmd = build_setxkbmap_line(app);
        g_string_append_printf(s,
            "command -v setxkbmap >/dev/null 2>&1 && %s\n", cmd);
        g_free(cmd);
    }
    return g_string_free(s, FALSE);
}

/* systemd environment.d: KEY=VALUE, zonder export */
static gchar *build_envd_body(App *app)
{
    const gchar *lang = combo_id(app->lang_combo);
    const gchar *region = combo_id(app->region_combo);
    GString *s = g_string_new("# geschreven door lxlocale\n");
    guint i;

    if (*lang)
        g_string_append_printf(s, "LANG=%s\n", lang);
    if (*region && g_strcmp0(region, lang))
        for (i = 0; REGION_VARS[i]; i++)
            g_string_append_printf(s, "%s=%s\n", REGION_VARS[i], region);

    return g_string_free(s, FALSE);
}

/* ------------------------------------------------------------------ */
/* combo's vullen                                                      */
/* ------------------------------------------------------------------ */

static void fill_variant_combo(GtkWidget *combo, const gchar *layout,
                               const gchar *select)
{
    GtkComboBoxText *c = GTK_COMBO_BOX_TEXT(combo);
    guint i;

    gtk_combo_box_text_remove_all(c);
    gtk_combo_box_text_append(c, "", "Standaard");
    if (layout && *layout) {
        for (i = 0; i < xkb_variants->len; i++) {
            XkbItem *it = g_ptr_array_index(xkb_variants, i);
            if (!g_strcmp0(it->parent, layout))
                gtk_combo_box_text_append(c, it->code, it->desc);
        }
    }
    if (!select || !*select ||
        !gtk_combo_box_set_active_id(GTK_COMBO_BOX(combo), select))
        gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
}

static void on_layout_changed(GtkComboBox *combo, gpointer data)
{
    App *app = data;
    gboolean was_loading = app->loading;

    app->loading = TRUE;
    if ((GtkWidget *) combo == app->layout_combo)
        fill_variant_combo(app->variant_combo, combo_id(app->layout_combo), NULL);
    else
        fill_variant_combo(app->variant2_combo, combo_id(app->layout2_combo), NULL);
    app->loading = was_loading;

    apply_xkb_now(app);
}

static void on_xkb_changed(GtkComboBox *combo, gpointer data)
{
    (void) combo;
    apply_xkb_now(data);
}

/* ------------------------------------------------------------------ */
/* toepassen                                                           */
/* ------------------------------------------------------------------ */

static void append_result(GString *msg, const gchar *path, gboolean ok,
                          const gchar *err)
{
    if (ok)
        g_string_append_printf(msg, "\342\234\223 %s\n", path);
    else
        g_string_append_printf(msg, "\342\234\227 %s  (%s)\n", path,
                               err ? err : "mislukt");
}

static void save_settings(App *app)
{
    GString *msg = g_string_new("");
    const gchar *home = g_get_home_dir();
    GError *err = NULL;
    gchar *path, *body;
    gboolean ok;

    /* 1. ~/.profile - alleen taalvariabelen (geen X-opdrachten) */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_profile))) {
        path = g_build_filename(home, ".profile", NULL);
        body = build_shell_body(app, FALSE);
        ok = write_block(path, body, FALSE, &err);
        append_result(msg, path, ok, err ? err->message : NULL);
        g_clear_error(&err);
        g_free(body);
        g_free(path);
    }

    /* 2. ~/.xprofile - taal en toetsenbord, door de meeste displaymanagers
     *    ingelezen voordat de window manager start */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_xprofile))) {
        path = g_build_filename(home, ".xprofile", NULL);
        body = build_shell_body(app, TRUE);
        ok = write_block(path, body, FALSE, &err);
        append_result(msg, path, ok, err ? err->message : NULL);
        g_clear_error(&err);
        g_free(body);
        g_free(path);
    }

    /* 3. ~/.xinitrc - alleen aanpassen als het al bestaat: een nieuw
     *    .xinitrc zonder "exec <wm>" zou startx onbruikbaar maken */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_xinitrc))) {
        path = g_build_filename(home, ".xinitrc", NULL);
        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            body = build_shell_body(app, TRUE);
            ok = write_block(path, body, TRUE, &err);
            append_result(msg, path, ok, err ? err->message : NULL);
            g_clear_error(&err);
            g_free(body);
        } else {
            g_string_append_printf(msg,
                "- %s overgeslagen (bestaat niet; zelf aanmaken)\n", path);
        }
        g_free(path);
    }

    /* 4. ~/.fluxbox/startup - idem, alleen als het bestaat */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_fluxbox))) {
        path = g_build_filename(home, ".fluxbox", "startup", NULL);
        if (g_file_test(path, G_FILE_TEST_EXISTS)) {
            body = build_shell_body(app, TRUE);
            ok = write_block(path, body, TRUE, &err);
            append_result(msg, path, ok, err ? err->message : NULL);
            g_clear_error(&err);
            g_free(body);
        } else {
            g_string_append_printf(msg,
                "- %s overgeslagen (bestaat niet)\n", path);
        }
        g_free(path);
    }

    /* 5. ~/.config/environment.d/60-lxlocale.conf (systemd-sessies) */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_envd))) {
        gchar *dir = g_build_filename(g_get_user_config_dir(),
                                      "environment.d", NULL);
        g_mkdir_with_parents(dir, 0755);
        path = g_build_filename(dir, ENVD_FILE, NULL);
        body = build_envd_body(app);
        ok = g_file_set_contents(path, body, -1, &err);
        append_result(msg, path, ok, err ? err->message : NULL);
        g_clear_error(&err);
        g_free(body);
        g_free(path);
        g_free(dir);
    }

    /* 6. systeembreed via localectl (vraagt om rechten met pkexec) */
    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_system))) {
        if (!have_program("localectl")) {
            g_string_append(msg, "- systeembreed overgeslagen: localectl ontbreekt\n");
        } else {
            const gchar *lang = combo_id(app->lang_combo);
            const gchar *region = combo_id(app->region_combo);
            gchar *langarg = g_strdup_printf("LANG=%s", lang);
            gchar *layout = current_layout_string(app);
            gchar *variant = current_variant_string(app);
            gboolean root = (geteuid() == 0);
            GPtrArray *a;
            guint i;

            a = g_ptr_array_new_with_free_func(g_free);
            if (!root)
                g_ptr_array_add(a, g_strdup("pkexec"));
            g_ptr_array_add(a, g_strdup("localectl"));
            g_ptr_array_add(a, g_strdup("set-locale"));
            g_ptr_array_add(a, langarg);
            if (*region && g_strcmp0(region, lang))
                for (i = 0; REGION_VARS[i]; i++)
                    g_ptr_array_add(a, g_strdup_printf("%s=%s", REGION_VARS[i], region));
            g_ptr_array_add(a, NULL);
            ok = run_argv((gchar **) a->pdata);
            append_result(msg, "localectl set-locale", ok, "geen rechten of fout");
            g_ptr_array_free(a, TRUE);

            a = g_ptr_array_new_with_free_func(g_free);
            if (!root)
                g_ptr_array_add(a, g_strdup("pkexec"));
            g_ptr_array_add(a, g_strdup("localectl"));
            g_ptr_array_add(a, g_strdup("set-x11-keymap"));
            g_ptr_array_add(a, g_strdup(layout));
            g_ptr_array_add(a, g_strdup(combo_id(app->model_combo)));
            g_ptr_array_add(a, g_strdup(variant));
            g_ptr_array_add(a, g_strdup(combo_id(app->switch_combo)));
            g_ptr_array_add(a, NULL);
            ok = run_argv((gchar **) a->pdata);
            append_result(msg, "localectl set-x11-keymap", ok, "geen rechten of fout");
            g_ptr_array_free(a, TRUE);

            g_free(layout);
            g_free(variant);
        }
    }

    if (msg->len == 0)
        g_string_append(msg, "Niets geselecteerd om op te slaan.\n");
    g_string_append(msg,
        "\nDe toetsenbordindeling is meteen actief. "
        "De taalinstelling wordt pas actief na opnieuw aanmelden.");

    {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(app->window),
                                              GTK_DIALOG_MODAL,
                                              GTK_MESSAGE_INFO,
                                              GTK_BUTTONS_OK,
                                              "%s", msg->str);
        gtk_window_set_title(GTK_WINDOW(d), "Opgeslagen");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
    }
    g_string_free(msg, TRUE);
}

static void on_apply(GtkButton *btn, gpointer data)
{
    App *app = data;

    (void) btn;
    apply_xkb_now(app);
    save_settings(app);
    app->applied = TRUE;
}

static void on_close(GtkButton *btn, gpointer data)
{
    App *app = data;

    (void) btn;
    gtk_widget_destroy(app->window);
}

static void on_destroy(GtkWidget *w, gpointer data)
{
    App *app = data;

    (void) w;
    /* niet toegepast? dan de indeling terugzetten zoals hij was */
    if (!app->applied)
        xkb_apply(app->orig_model, app->orig_layout,
                  app->orig_variant, app->orig_option);
    gtk_main_quit();
}

/* ------------------------------------------------------------------ */
/* opbouw van de interface                                             */
/* ------------------------------------------------------------------ */

static void grid_attach_row(GtkWidget *grid, gint row, const gchar *label,
                            GtkWidget *widget)
{
    GtkWidget *l = gtk_label_new(label);

    gtk_widget_set_halign(l, GTK_ALIGN_START);
    gtk_grid_attach(GTK_GRID(grid), l, 0, row, 1, 1);
    gtk_widget_set_hexpand(widget, TRUE);
    gtk_grid_attach(GTK_GRID(grid), widget, 1, row, 1, 1);
}

static GtkWidget *new_page_grid(void)
{
    GtkWidget *grid = gtk_grid_new();

    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    return grid;
}

static GtkWidget *build_language_page(App *app, GPtrArray *locales)
{
    GtkWidget *grid = new_page_grid();
    GtkWidget *hint;
    const gchar *cur_lang = g_getenv("LANG");
    const gchar *cur_time = g_getenv("LC_TIME");
    gchar *norm_lang = normalize_locale(cur_lang ? cur_lang : "C.UTF-8");
    gchar *norm_time = cur_time ? normalize_locale(cur_time) : NULL;
    guint i;

    app->lang_combo = gtk_combo_box_text_new();
    app->region_combo = gtk_combo_box_text_new();

    for (i = 0; i < locales->len; i++) {
        const gchar *loc = g_ptr_array_index(locales, i);
        gchar *label = locale_label(loc);

        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->lang_combo), loc, label);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->region_combo), loc, label);
        g_free(label);
    }
    if (!gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->lang_combo), norm_lang))
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->lang_combo), 0);
    if (!norm_time ||
        !gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->region_combo), norm_time))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->region_combo), norm_lang);

    grid_attach_row(grid, 0, "Taal (LANG):", app->lang_combo);
    grid_attach_row(grid, 1, "Opmaak (datum, getallen):", app->region_combo);

    hint = gtk_label_new(
        "De lijst toont de locales die op dit systeem gegenereerd zijn.\n"
        "Ontbreekt er een taal, voeg hem toe aan /etc/locale.gen en\n"
        "draai daarna locale-gen (of dpkg-reconfigure locales).");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_widget_set_margin_top(hint, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
    gtk_grid_attach(GTK_GRID(grid), hint, 0, 2, 2, 1);

    g_free(norm_lang);
    g_free(norm_time);
    return grid;
}

static GtkWidget *build_keyboard_page(App *app)
{
    GtkWidget *grid = new_page_grid();
    gchar **cur_layouts = NULL;
    gchar **cur_variants = NULL;
    guint i;

    app->model_combo   = gtk_combo_box_text_new();
    app->layout_combo  = gtk_combo_box_text_new();
    app->variant_combo = gtk_combo_box_text_new();
    app->layout2_combo = gtk_combo_box_text_new();
    app->variant2_combo = gtk_combo_box_text_new();
    app->switch_combo  = gtk_combo_box_text_new();
    app->test_entry    = gtk_entry_new();

    for (i = 0; i < xkb_models->len; i++) {
        XkbItem *it = g_ptr_array_index(xkb_models, i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->model_combo),
                                  it->code, it->desc);
    }
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->layout2_combo), "", "Geen");
    for (i = 0; i < xkb_layouts->len; i++) {
        XkbItem *it = g_ptr_array_index(xkb_layouts, i);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->layout_combo),
                                  it->code, it->desc);
        gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->layout2_combo),
                                  it->code, it->desc);
    }
    gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->switch_combo), "", "Geen");
    for (i = 0; i < xkb_options->len; i++) {
        XkbItem *it = g_ptr_array_index(xkb_options, i);
        if (g_str_has_prefix(it->code, "grp:"))
            gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(app->switch_combo),
                                      it->code, it->desc);
    }

    /* huidige situatie voorselecteren */
    if (app->orig_layout)
        cur_layouts = g_strsplit(app->orig_layout, ",", -1);
    if (app->orig_variant)
        cur_variants = g_strsplit(app->orig_variant, ",", -1);

    if (!app->orig_model ||
        !gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->model_combo), app->orig_model))
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->model_combo), "pc105");

    if (cur_layouts && cur_layouts[0])
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->layout_combo), cur_layouts[0]);
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(app->layout_combo)) < 0)
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->layout_combo), "us");

    fill_variant_combo(app->variant_combo, combo_id(app->layout_combo),
                       (cur_variants && cur_variants[0]) ? cur_variants[0] : NULL);

    if (cur_layouts && cur_layouts[0] && cur_layouts[1])
        gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->layout2_combo), cur_layouts[1]);
    if (gtk_combo_box_get_active(GTK_COMBO_BOX(app->layout2_combo)) < 0)
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->layout2_combo), 0);

    fill_variant_combo(app->variant2_combo, combo_id(app->layout2_combo),
                       (cur_variants && cur_variants[0] && cur_variants[1])
                       ? cur_variants[1] : NULL);

    if (!app->orig_option ||
        !gtk_combo_box_set_active_id(GTK_COMBO_BOX(app->switch_combo), app->orig_option))
        gtk_combo_box_set_active(GTK_COMBO_BOX(app->switch_combo), 0);

    gtk_entry_set_placeholder_text(GTK_ENTRY(app->test_entry),
                                   "Typ hier om de indeling te testen");

    grid_attach_row(grid, 0, "Model:", app->model_combo);
    grid_attach_row(grid, 1, "Indeling:", app->layout_combo);
    grid_attach_row(grid, 2, "Variant:", app->variant_combo);
    grid_attach_row(grid, 3, "Tweede indeling:", app->layout2_combo);
    grid_attach_row(grid, 4, "Variant:", app->variant2_combo);
    grid_attach_row(grid, 5, "Wisseltoets:", app->switch_combo);
    grid_attach_row(grid, 6, "Proberen:", app->test_entry);

    g_signal_connect(app->model_combo, "changed", G_CALLBACK(on_xkb_changed), app);
    g_signal_connect(app->layout_combo, "changed", G_CALLBACK(on_layout_changed), app);
    g_signal_connect(app->layout2_combo, "changed", G_CALLBACK(on_layout_changed), app);
    g_signal_connect(app->variant_combo, "changed", G_CALLBACK(on_xkb_changed), app);
    g_signal_connect(app->variant2_combo, "changed", G_CALLBACK(on_xkb_changed), app);
    g_signal_connect(app->switch_combo, "changed", G_CALLBACK(on_xkb_changed), app);

    g_strfreev(cur_layouts);
    g_strfreev(cur_variants);
    return grid;
}

static GtkWidget *build_save_page(App *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *hint;

    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    app->chk_profile  = gtk_check_button_new_with_label(
        "~/.profile  (taal, voor tekstsessies)");
    app->chk_xprofile = gtk_check_button_new_with_label(
        "~/.xprofile  (taal + toetsenbord, meeste displaymanagers)");
    app->chk_xinitrc  = gtk_check_button_new_with_label(
        "~/.xinitrc  (bij startx, alleen als het bestand bestaat)");
    app->chk_fluxbox  = gtk_check_button_new_with_label(
        "~/.fluxbox/startup  (alleen als het bestand bestaat)");
    app->chk_envd     = gtk_check_button_new_with_label(
        "~/.config/environment.d/" ENVD_FILE "  (systemd-sessies)");
    app->chk_system   = gtk_check_button_new_with_label(
        "Systeembreed via localectl  (vraagt om beheerdersrechten)");

    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chk_xprofile), TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chk_envd), TRUE);

    gtk_box_pack_start(GTK_BOX(box), app->chk_profile, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), app->chk_xprofile, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), app->chk_xinitrc, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), app->chk_fluxbox, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), app->chk_envd, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), app->chk_system, FALSE, FALSE, 0);

    hint = gtk_label_new(
        "lxlocale schrijft alleen tussen de regels\n"
        BLOCK_BEGIN " en " BLOCK_END ",\n"
        "de rest van je bestanden blijft ongemoeid.");
    gtk_label_set_xalign(GTK_LABEL(hint), 0.0);
    gtk_widget_set_margin_top(hint, 8);
    gtk_style_context_add_class(gtk_widget_get_style_context(hint), "dim-label");
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    return box;
}

int main(int argc, char *argv[])
{
    App app;
    GtkWidget *vbox, *notebook, *bbox, *apply_btn, *close_btn;
    GPtrArray *locales;

    gtk_init(&argc, &argv);

    memset(&app, 0, sizeof(app));
    app.loading = TRUE;

    load_xkb_rules();
    locales = load_locales();
    xkb_query(&app.orig_model, &app.orig_layout, &app.orig_variant,
              &app.orig_option);

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), "Taal en toetsenbord");
    gtk_window_set_default_size(GTK_WINDOW(app.window), 520, 340);
    gtk_window_set_position(GTK_WINDOW(app.window), GTK_WIN_POS_CENTER);
    gtk_window_set_icon_name(GTK_WINDOW(app.window), "preferences-desktop-locale");

    vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_container_set_border_width(GTK_CONTAINER(vbox), 6);
    gtk_container_add(GTK_CONTAINER(app.window), vbox);

    notebook = gtk_notebook_new();
    gtk_box_pack_start(GTK_BOX(vbox), notebook, TRUE, TRUE, 0);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             build_language_page(&app, locales),
                             gtk_label_new("Taal"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             build_keyboard_page(&app),
                             gtk_label_new("Toetsenbord"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook),
                             build_save_page(&app),
                             gtk_label_new("Opslaan in"));

    bbox = gtk_button_box_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_button_box_set_layout(GTK_BUTTON_BOX(bbox), GTK_BUTTONBOX_END);
    gtk_box_set_spacing(GTK_BOX(bbox), 6);
    gtk_box_pack_start(GTK_BOX(vbox), bbox, FALSE, FALSE, 0);

    close_btn = gtk_button_new_with_mnemonic("_Sluiten");
    apply_btn = gtk_button_new_with_mnemonic("_Toepassen");
    gtk_container_add(GTK_CONTAINER(bbox), close_btn);
    gtk_container_add(GTK_CONTAINER(bbox), apply_btn);

    g_signal_connect(apply_btn, "clicked", G_CALLBACK(on_apply), &app);
    g_signal_connect(close_btn, "clicked", G_CALLBACK(on_close), &app);
    g_signal_connect(app.window, "destroy", G_CALLBACK(on_destroy), &app);

    if (!have_program("setxkbmap")) {
        GtkWidget *d = gtk_message_dialog_new(GTK_WINDOW(app.window),
            GTK_DIALOG_MODAL, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK,
            "setxkbmap is niet gevonden.\n\n"
            "De toetsenbordindeling kan wel opgeslagen worden, maar niet "
            "meteen actief gemaakt. Installeer x11-xkb-utils of "
            "x11-xserver-utils.");
        gtk_dialog_run(GTK_DIALOG(d));
        gtk_widget_destroy(d);
    }

    gtk_widget_show_all(app.window);
    app.loading = FALSE;
    gtk_main();

    g_free(app.orig_model);
    g_free(app.orig_layout);
    g_free(app.orig_variant);
    g_free(app.orig_option);
    g_ptr_array_free(locales, TRUE);
    return 0;
}
