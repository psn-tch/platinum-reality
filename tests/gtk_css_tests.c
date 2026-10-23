#include <gtk/gtk.h>

static unsigned errors;
static void parse_error(GtkCssProvider *provider, GtkCssSection *section, const GError *error,
                        gpointer data)
{
    (void)provider;
    (void)section;
    (void)data;
    ++errors;
    g_test_message("CSS parser: %s", error->message);
}
static void parses(void)
{
    GtkCssProvider *provider = gtk_css_provider_new();
    g_signal_connect(provider, "parsing-error", G_CALLBACK(parse_error), NULL);
#if GTK_MAJOR_VERSION == 3
    GError *error = NULL;
    g_assert_true(gtk_css_provider_load_from_path(provider, PLATINUM_CSS_FILE, &error));
    g_assert_no_error(error);
#else
    gtk_css_provider_load_from_path(provider, PLATINUM_CSS_FILE);
#endif
    g_assert_cmpuint(errors, ==, 0);
    g_object_unref(provider);
}
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/platinum/css-parses-with-installed-gtk", parses);
    return g_test_run();
}
