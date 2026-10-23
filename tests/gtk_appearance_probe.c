#include <gtk/gtk.h>

/* Owned disposable GTK client, available only in the explicit native venue. */
static GtkWidget *entry, *button, *check, *window;
static GMainLoop *loop;
static int clicks;
static guint timer;
static void unrealized(GtkWidget *widget, gpointer data)
{
    (void)widget;
    (void)data;
    // Closing a disposable probe must end its private loop too. Otherwise its
    // D-Bus object could outlive the widgets it reports on.
    entry = button = check = window = NULL;
    g_main_loop_quit(loop);
}
static void clicked(GtkButton *widget, gpointer data)
{
    (void)widget;
    (void)data;
    ++clicks;
}
static gboolean expired(gpointer data)
{
    (void)data;
    timer = 0;
    g_main_loop_quit(loop);
    return G_SOURCE_REMOVE;
}
static void method(GDBusConnection *connection, const gchar *sender, const gchar *path,
                   const gchar *interface, const gchar *name, GVariant *parameters,
                   GDBusMethodInvocation *invocation, gpointer data)
{
    (void)connection;
    (void)sender;
    (void)path;
    (void)interface;
    (void)parameters;
    (void)data;
    if (g_str_equal(name, "Finish"))
    {
        g_dbus_method_invocation_return_value(invocation, NULL);
        g_main_loop_quit(loop);
        return;
    }
    if (!window || !entry || !button || !check)
    {
        g_dbus_method_invocation_return_dbus_error(
            invocation, "org.frankenstein.GtkProbe.Closed", "The disposable window has closed.");
        return;
    }
    GVariantBuilder state;
    g_variant_builder_init(&state, G_VARIANT_TYPE("a{sv}"));
    g_variant_builder_add(&state, "{sv}", "clicks", g_variant_new_int32(clicks));
#if GTK_MAJOR_VERSION == 3
    const char *text = gtk_entry_get_text(GTK_ENTRY(entry));
#else
    const char *text = gtk_editable_get_text(GTK_EDITABLE(entry));
#endif
    g_variant_builder_add(&state, "{sv}", "text", g_variant_new_string(text));
    GtkWidget *focus = gtk_window_get_focus(GTK_WINDOW(window));
    g_variant_builder_add(
        &state, "{sv}", "entryFocused",
        g_variant_new_boolean(focus && (focus == entry || gtk_widget_is_ancestor(focus, entry))));
#if GTK_MAJOR_VERSION == 3
    gboolean checked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check));
#else
    gboolean checked = gtk_check_button_get_active(GTK_CHECK_BUTTON(check));
#endif
    g_variant_builder_add(&state, "{sv}", "checked", g_variant_new_boolean(checked));
    PangoContext *context = gtk_widget_get_pango_context(button);
    char *font = pango_font_description_to_string(pango_context_get_font_description(context));
    g_variant_builder_add(&state, "{sv}", "font", g_variant_new_string(font));
    g_variant_builder_add(&state, "{sv}", "configHome", g_variant_new_string(g_get_user_config_dir()));
    g_free(font);
    g_dbus_method_invocation_return_value(invocation, g_variant_new("(a{sv})", &state));
}
int main(int argc, char **argv)
{
    if (g_strcmp0(g_getenv("FRANKENSTEIN_NATIVE_TEST"), "1") != 0)
        return 2;
    const char *suffix = GTK_MAJOR_VERSION == 3 ? "3" : "4";
    char *identity = g_strconcat("org.frankenstein.GtkProbe", suffix, NULL);
    g_set_prgname(identity);
    /* Predictable owned test rendering; no user application's backend is changed. */
    g_setenv("GSK_RENDERER", "cairo", TRUE);
#if GTK_MAJOR_VERSION == 3
    gtk_init(&argc, &argv);
    window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
#else
    (void)argc;
    (void)argv;
    gtk_init();
    window = gtk_window_new();
#endif
    char *title = g_strconcat("GTK ", suffix, " Platinum Probe", NULL);
    gtk_window_set_title(GTK_WINDOW(window), title);
    gtk_window_set_decorated(GTK_WINDOW(window), FALSE);
    gtk_window_set_default_size(GTK_WINDOW(window), 420, 310);
    GtkWidget *fixed = gtk_fixed_new();
#if GTK_MAJOR_VERSION == 3
    gtk_container_add(GTK_CONTAINER(window), fixed);
#else
    gtk_window_set_child(GTK_WINDOW(window), fixed);
#endif
    GtkWidget *label = gtk_label_new(title);
    gtk_fixed_put(GTK_FIXED(fixed), label, 16, 14);
    entry = gtk_entry_new();
    gtk_widget_set_size_request(entry, 360, 30);
    gtk_fixed_put(GTK_FIXED(fixed), entry, 16, 48);
    button = gtk_button_new_with_label("Press a classic button");
    gtk_widget_set_size_request(button, 220, 30);
    gtk_fixed_put(GTK_FIXED(fixed), button, 16, 96);
    g_signal_connect(button, "clicked", G_CALLBACK(clicked), NULL);
    check = gtk_check_button_new_with_label("Keep modern functionality");
    gtk_fixed_put(GTK_FIXED(fixed), check, 16, 146);
    GtkWidget *scale = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    gtk_widget_set_size_request(scale, 340, 35);
    gtk_fixed_put(GTK_FIXED(fixed), scale, 16, 190);
    GtkWidget *radio = gtk_check_button_new_with_label("Platinum application controls");
    gtk_fixed_put(GTK_FIXED(fixed), radio, 16, 250);
    GError *error = NULL;
    GDBusConnection *bus = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    if (!bus)
        return 3;
    const char *xml = "<node><interface name='org.frankenstein.GtkProbe'>"
                      "<method name='State'><arg type='a{sv}' direction='out'/></method>"
                      "<method name='Finish'/></interface></node>";
    GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, &error);
    static const GDBusInterfaceVTable vtable = {.method_call = method};
    guint registration = info ? g_dbus_connection_register_object(
                                   bus, "/Probe", info->interfaces[0], &vtable, NULL, NULL, &error)
                              : 0;
    if (!registration)
        return 4;
    guint owner = g_bus_own_name_on_connection(bus, identity, G_BUS_NAME_OWNER_FLAGS_NONE, NULL,
                                               NULL, NULL, NULL);
    loop = g_main_loop_new(NULL, FALSE);
    g_signal_connect(window, "unrealize", G_CALLBACK(unrealized), NULL);
    timer = g_timeout_add_seconds(30, expired, NULL);
#if GTK_MAJOR_VERSION == 3
    gtk_widget_show_all(window);
#else
    gtk_window_present(GTK_WINDOW(window));
#endif
    g_main_loop_run(loop);
    if (timer)
        g_source_remove(timer);
    g_dbus_connection_unregister_object(bus, registration);
    g_bus_unown_name(owner);
    g_dbus_connection_flush_sync(bus, NULL, NULL);
    g_dbus_node_info_unref(info);
    g_object_unref(bus);
    g_main_loop_unref(loop);
    g_free(identity);
    g_free(title);
    return 0;
}
