// Asks the X11 window manager for what Steam's web views and xdotool ask with client messages to
// the root window, and nothing else (no XSetInputFocus fallback, unlike Qt). Finds the window by a
// substring of its title.
//   x11-activate-probe TITLE                    _NET_ACTIVE_WINDOW (come forward)
//   x11-activate-probe TITLE move|size-br X Y   _NET_WM_MOVERESIZE with button 1 held at root X,Y
//                                               (a drag on its own title bar or bottom-right edge)
// Prints "sent 0xWINDOW" or "not-found".
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xcb/xcb.h>

static xcb_atom_t atom(xcb_connection_t *c, const char *name)
{
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c, xcb_intern_atom(c, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    return a;
}

// Only a window on screen counts: Qt keeps hidden windows with the same title (its client leader).
static int viewable(xcb_connection_t *c, xcb_window_t w)
{
    xcb_get_window_attributes_reply_t *a = xcb_get_window_attributes_reply(c, xcb_get_window_attributes(c, w), NULL);
    const int result = a && a->map_state == XCB_MAP_STATE_VIEWABLE;
    free(a);
    return result;
}

static int titled(xcb_connection_t *c, xcb_window_t w, xcb_atom_t netName, xcb_atom_t utf8, const char *want)
{
    if (!viewable(c, w))
        return 0;
    const xcb_atom_t names[] = {netName, XCB_ATOM_WM_NAME};
    const xcb_atom_t types[] = {utf8, XCB_ATOM_STRING};
    for (int i = 0; i < 2; ++i) {
        xcb_get_property_reply_t *p =
            xcb_get_property_reply(c, xcb_get_property(c, 0, w, names[i], types[i], 0, 256), NULL);
        int found = 0;
        if (p && xcb_get_property_value_length(p) > 0) {
            int length = xcb_get_property_value_length(p);
            char *text = strndup(xcb_get_property_value(p), (size_t)length);
            found = text && strstr(text, want) != NULL;
            free(text);
        }
        free(p);
        if (found)
            return 1;
    }
    return 0;
}

static xcb_window_t find(xcb_connection_t *c, xcb_window_t parent, xcb_atom_t netName, xcb_atom_t utf8, const char *want)
{
    xcb_query_tree_reply_t *tree = xcb_query_tree_reply(c, xcb_query_tree(c, parent), NULL);
    if (!tree)
        return XCB_WINDOW_NONE;
    xcb_window_t *children = xcb_query_tree_children(tree);
    xcb_window_t result = XCB_WINDOW_NONE;
    for (int i = 0; i < xcb_query_tree_children_length(tree) && result == XCB_WINDOW_NONE; ++i)
        result = titled(c, children[i], netName, utf8, want) ? children[i] : find(c, children[i], netName, utf8, want);
    free(tree);
    return result;
}

int main(int argc, char **argv)
{
    if (argc != 2 && argc != 5) {
        fprintf(stderr, "usage: x11-activate-probe TITLE [move|size-br X Y]\n");
        return 2;
    }
    xcb_connection_t *c = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(c)) {
        fprintf(stderr, "cannot connect to X\n");
        return 1;
    }
    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    xcb_window_t target = find(c, screen->root, atom(c, "_NET_WM_NAME"), atom(c, "UTF8_STRING"), argv[1]);
    if (target == XCB_WINDOW_NONE) {
        puts("not-found");
        xcb_disconnect(c);
        return 1;
    }
    xcb_client_message_event_t event = {0};
    event.response_type = XCB_CLIENT_MESSAGE;
    event.format = 32;
    event.window = target;
    if (argc == 5) {
        event.type = atom(c, "_NET_WM_MOVERESIZE");
        event.data.data32[0] = (uint32_t)atoi(argv[3]);
        event.data.data32[1] = (uint32_t)atoi(argv[4]);
        event.data.data32[2] = strcmp(argv[2], "move") == 0 ? 8 : 4; // _NET_WM_MOVERESIZE_MOVE or SIZE_BOTTOMRIGHT
        event.data.data32[3] = 1; // Button 1.
        event.data.data32[4] = 1; // Source: an application.
    } else {
        event.type = atom(c, "_NET_ACTIVE_WINDOW");
        event.data.data32[0] = 1; // Source: an application.
        event.data.data32[1] = XCB_CURRENT_TIME;
    }
    xcb_send_event(c, 0, screen->root, XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY,
                   (const char *)&event);
    xcb_flush(c);
    printf("sent 0x%x\n", target);
    xcb_disconnect(c);
    return 0;
}
