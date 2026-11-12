#include "policy/transition.hpp"
#include <QtTest>

using namespace frankenstein::policy;
namespace
{
State initial()
{
    State state;
    state.session = SessionId{17};
    return state;
}
void dispatch(State &state, Event event)
{
    auto result = transition(state, {state.session, std::move(event)});
    if (result.status == Status::rejected)
        qFatal("Unexpected policy rejection %d", int(result.rejection));
    state = std::move(result.next);
}
void addApp(State &state, std::uint64_t app, std::uint64_t window)
{
    dispatch(state, RegisterApplication{ApplicationId{app}});
    dispatch(state,
             RegisterWindow{WindowId{window}, ApplicationId{app}, {{40, 60}, {640, 480}}, true});
}
// Laptop panel (primary) at 0,0 and a dock monitor to its right.
void twoScreens(State &state)
{
    dispatch(state, SetScreens{{Screen{"eDP-1", {{0, 0}, {1920, 1080}}},
                                Screen{"DP-3", {{1920, 0}, {2560, 1440}}}}});
}
void addAppAt(State &state, std::uint64_t app, std::uint64_t window, Point at)
{
    dispatch(state, RegisterApplication{ApplicationId{app}});
    dispatch(state, RegisterWindow{WindowId{window}, ApplicationId{app}, {at, {640, 480}}, true});
}
} // namespace
class PolicyTests : public QObject
{
    Q_OBJECT
  private slots:
    void workspaceSwitchHidesOnlyThatScreensWindows()
    {
        auto s = initial(); twoScreens(s);
        dispatch(s, RegisterApplication{{1}, true}); // the Finder, owner of the desktop
        addAppAt(s, 2, 2, {100, 100}); addAppAt(s, 3, 3, {2100, 100});
        dispatch(s, ActivateWindow{{2}});
        auto shown = transition(s, {s.session, ShowWorkspace{0, 2}});
        QCOMPARE(shown.status, Status::applied);
        s = shown.next;
        QCOMPARE(presentation(s, {2}), Presentation::hidden);
        QCOMPARE(presentation(s, {3}), Presentation::body); // the dock keeps its workspace 1
        QCOMPARE(visible_order(s), (std::vector<WindowId>{{3}}));
        // Nothing on workspace 2 of this screen: the desktop comes forward, as with Spaces.
        QCOMPARE(s.active, (Context{ApplicationId{1}, std::nullopt, true}));
        bool hidden = false;
        for (const auto &effect : shown.effects)
            if (const auto *present = std::get_if<PresentWindow>(&effect))
                hidden |= present->window == WindowId{2} && present->presentation == Presentation::hidden;
        QVERIFY(hidden);
        // A window opened now joins workspace 2 of the screen it appears on.
        dispatch(s, RegisterWindow{{4}, {2}, {{300, 300}, {400, 300}}, true});
        QCOMPARE(s.windows.at({4}).workspace, 2u);
        dispatch(s, ActivateWindow{{4}});
        dispatch(s, ShowWorkspace{0, 1});
        QCOMPARE(presentation(s, {2}), Presentation::body);
        QCOMPARE(presentation(s, {4}), Presentation::hidden);
        QCOMPARE(s.active.window, std::optional<WindowId>{WindowId{2}});
    }
    void movingAWindowTakesItsDialogsAndFocusesTheNext()
    {
        auto s = initial(); twoScreens(s);
        addAppAt(s, 1, 1, {100, 100}); addAppAt(s, 2, 2, {200, 150});
        dispatch(s, RegisterWindow{{3}, {2}, {{250, 200}, {300, 200}}, false, WindowId{2}, false});
        dispatch(s, ActivateWindow{{1}}); dispatch(s, ActivateWindow{{2}});
        dispatch(s, MoveToWorkspace{{3}, 4}); // asking for the dialog moves the whole family
        QCOMPARE(s.windows.at({2}).workspace, 4u); QCOMPARE(s.windows.at({3}).workspace, 4u);
        QCOMPARE(presentation(s, {2}), Presentation::hidden);
        QCOMPARE(presentation(s, {3}), Presentation::hidden);
        QCOMPARE(s.active.window, std::optional<WindowId>{WindowId{1}});
        QCOMPARE(s.screens[0].workspace, 1u); // Sway doesn't follow the window
        // Choosing the application again goes to the workspace its window is on.
        dispatch(s, ActivateApplication{{2}});
        QCOMPARE(s.screens[0].workspace, 4u);
        QCOMPARE(s.active.application, std::optional<ApplicationId>{ApplicationId{2}});
        QCOMPARE(presentation(s, {2}), Presentation::body);
        QCOMPARE(presentation(s, {1}), Presentation::hidden);
        // So does going to a window directly (the Window menu, an activation request).
        dispatch(s, ActivateWindow{{1}});
        QCOMPARE(s.screens[0].workspace, 1u);
        QCOMPARE(s.active.window, std::optional<WindowId>{WindowId{1}});
    }
    void draggingToAnotherScreenJoinsItsWorkspaceButRescuesDont()
    {
        auto s = initial(); twoScreens(s);
        addAppAt(s, 1, 1, {100, 100});
        dispatch(s, ShowWorkspace{1, 3});
        dispatch(s, ActivateWindow{{1}});
        dispatch(s, BeginWindowMove{{1}});
        dispatch(s, UpdateWindowMove{{1}, {2200, 200}});
        dispatch(s, EndWindowMove{{1}, true});
        QCOMPARE(s.windows.at({1}).workspace, 3u);
        QCOMPARE(presentation(s, {1}), Presentation::body);
        QCOMPARE(s.active.window, std::optional<WindowId>{WindowId{1}});
        // A client-side drag back counts as the user's too.
        dispatch(s, SetPosition{{1}, {300, 200}, true});
        QCOMPARE(s.windows.at({1}).workspace, 1u);
        // A rescue (the dock monitor went away) keeps the window's workspace number.
        dispatch(s, MoveToWorkspace{{1}, 5});
        dispatch(s, SetPosition{{1}, {2300, 200}});
        QCOMPARE(s.windows.at({1}).workspace, 5u);
        QCOMPARE(presentation(s, {1}), Presentation::hidden);
    }
    void screensKeepTheirWorkspacesAcrossLayoutChanges()
    {
        auto s = initial(); twoScreens(s);
        dispatch(s, ShowWorkspace{0, 2}); dispatch(s, ShowWorkspace{1, 7});
        dispatch(s, SetScreens{{Screen{"DP-3", {{0, 0}, {2560, 1440}}},
                                Screen{"HDMI-A-1", {{2560, 0}, {1920, 1080}}}}});
        QCOMPARE(s.screens[0].workspace, 7u);
        QCOMPARE(s.screens[1].workspace, 1u);
        for (auto bad : {transition(s, {s.session, ShowWorkspace{2, 1}}),
                         transition(s, {s.session, ShowWorkspace{0, 0}}),
                         transition(s, {s.session, ShowWorkspace{0, workspace_count + 1}}),
                         transition(s, {s.session, SetScreens{}})})
        {
            QCOMPARE(bad.status, Status::rejected);
            QCOMPARE(bad.next, s);
        }
        QCOMPARE(transition(s, {s.session, ShowWorkspace{0, 7}}).status, Status::no_change);
    }
    void altTabVisitsOnlyWorkspacesWithWindows()
    {
        auto s = initial(); twoScreens(s);
        QCOMPARE(next_occupied_workspace(s, 0, 1), std::nullopt); // no windows anywhere
        addAppAt(s, 1, 1, {100, 100});
        QCOMPARE(next_occupied_workspace(s, 0, 1), std::nullopt); // only the one showing
        dispatch(s, ShowWorkspace{0, 3}); addAppAt(s, 2, 2, {100, 100});
        dispatch(s, ShowWorkspace{0, 5}); addAppAt(s, 3, 3, {100, 100});
        dispatch(s, ShowWorkspace{1, 8}); addAppAt(s, 4, 4, {2100, 100}); // the dock's own
        dispatch(s, ShowWorkspace{0, 3});
        QCOMPARE(occupied_workspaces(s, 0), (std::vector<std::uint32_t>{1, 3, 5}));
        QCOMPARE(occupied_workspaces(s, 1), (std::vector<std::uint32_t>{8}));
        QCOMPARE(next_occupied_workspace(s, 0, 1), std::optional<std::uint32_t>{5});
        QCOMPARE(next_occupied_workspace(s, 0, -1), std::optional<std::uint32_t>{1});
        dispatch(s, ShowWorkspace{0, 5});
        QCOMPARE(next_occupied_workspace(s, 0, 1), std::optional<std::uint32_t>{1}); // wraps
        dispatch(s, ShowWorkspace{0, 1});
        QCOMPARE(next_occupied_workspace(s, 0, -1), std::optional<std::uint32_t>{5});
        // From an empty workspace, the nearest one with windows that way.
        dispatch(s, ShowWorkspace{0, 4});
        QCOMPARE(next_occupied_workspace(s, 0, 1), std::optional<std::uint32_t>{5});
        QCOMPARE(next_occupied_workspace(s, 0, -1), std::optional<std::uint32_t>{3});
        // A hidden application's windows don't make a workspace worth visiting.
        dispatch(s, HideApplication{{3}});
        QCOMPARE(occupied_workspaces(s, 0), (std::vector<std::uint32_t>{1, 3}));
        QCOMPARE(next_occupied_workspace(s, 0, 1), std::optional<std::uint32_t>{1});
        QCOMPARE(next_occupied_workspace(s, 1, 1), std::nullopt);
        QCOMPARE(next_occupied_workspace(s, 2, 1), std::nullopt); // no such screen
    }
    void unpluggingAScreenKeepsItsShowingWindowsInView()
    {
        auto s = initial(); twoScreens(s);
        dispatch(s, ShowWorkspace{0, 3});
        dispatch(s, ShowWorkspace{1, 2});
        addAppAt(s, 1, 1, {2100, 100}); // showing on the dock's workspace 2
        dispatch(s, RegisterWindow{{2}, {1}, {{2200, 200}, {300, 200}}, false, WindowId{1}, false});
        dispatch(s, ShowWorkspace{1, 6});
        addAppAt(s, 3, 3, {2300, 300}); // on the dock's workspace 6, hidden once 2 shows again
        dispatch(s, ShowWorkspace{1, 2});
        dispatch(s, ActivateWindow{{1}});
        // The dock goes away; the laptop panel shows workspace 3.
        dispatch(s, SetScreens{{Screen{"eDP-1", {{0, 0}, {1920, 1080}}}}});
        QCOMPARE(s.windows.at({1}).workspace, 3u);
        QCOMPARE(s.windows.at({2}).workspace, 3u); // with its dialog
        QCOMPARE(s.windows.at({3}).workspace, 6u); // hidden there: it keeps its number
        // The display change then rescues the windows onto the panel.
        dispatch(s, SetPosition{{1}, {100, 100}});
        dispatch(s, SetPosition{{3}, {300, 300}});
        QCOMPARE(presentation(s, {1}), Presentation::body);
        QCOMPARE(presentation(s, {3}), Presentation::hidden);
        QCOMPARE(s.active.window, std::optional<WindowId>{WindowId{1}});
        QCOMPARE(occupied_workspaces(s, 0), (std::vector<std::uint32_t>{3, 6}));
    }
    void hiddenWorkspaceCancelsAMoveAndMenu()
    {
        auto s = initial(); addApp(s, 1, 1); addApp(s, 2, 2);
        dispatch(s, ActivateWindow{{1}});
        dispatch(s, BeginWindowMove{{1}});
        dispatch(s, ShowWorkspace{0, 2});
        QVERIFY(!s.move);
        QCOMPARE(s.active.window, std::nullopt);
        dispatch(s, ShowWorkspace{0, 1});
        dispatch(s, BeginMenuTracking{{1}});
        dispatch(s, MoveToWorkspace{{2}, 2}); // not the active window: the menu stays
        QVERIFY(s.menu);
        dispatch(s, MoveToWorkspace{{s.active.window->value}, 2});
        QVERIFY(!s.menu);
    }
    void outlineMoveDefersGeometryUntilCommit()
    {
        auto s = initial(); addApp(s,1,1); dispatch(s,ActivateWindow{{1}});
        const auto original = s.windows.at({1}).geometry;
        dispatch(s,BeginWindowMove{{1}});
        auto preview = transition(s,{s.session,UpdateWindowMove{{1},{-250,120}}});
        QCOMPARE(preview.next.windows.at({1}).geometry,original);
        QCOMPARE(preview.next.move->preview,(Point{-250,120}));
        QCOMPARE(preview.effects.size(),std::size_t(1));
        QVERIFY(std::holds_alternative<PublishMoveOutline>(preview.effects.front()));
        s=preview.next;
        dispatch(s,ObserveContentSize{{1},original.observed_size});
        QVERIFY(s.move); // Equal size acknowledgement must not abort the grab.
        dispatch(s,EndWindowMove{{1},true});
        QVERIFY(!s.move);
        QCOMPARE(s.windows.at({1}).geometry.position,(Point{-250,120}));
        QCOMPARE(s.windows.at({1}).geometry.observed_size,original.observed_size);
        auto late=transition(s,{s.session,EndWindowMove{{1},true}});
        QCOMPARE(late.rejection,Rejection::stale_window_move); QCOMPARE(late.next,s);
    }
    void cancelledAndInvalidatedMovesNeverCommitPreview()
    {
        for (int action=0;action<7;++action) {
            auto s=initial();addApp(s,1,1);addApp(s,2,2);dispatch(s,ActivateWindow{{1}});
            const auto original=s.windows.at({1}).geometry.position;
            dispatch(s,BeginWindowMove{{1}});dispatch(s,UpdateWindowMove{{1},{1600,80}});
            switch(action) {
            case 0:dispatch(s,EndWindowMove{{1},false});break;
            case 1:dispatch(s,ActivateWindow{{2}});break;
            case 2:dispatch(s,HideApplication{{1}});break;
            case 3:dispatch(s,RemoveWindow{{1}});break;
            case 4:dispatch(s,ObserveContentSize{{1},{800,500}});break;
            case 5:dispatch(s,BeginMenuTracking{{1}});break;
            case 6:dispatch(s,SetCollapsed{{1},true});break;
            }
            QVERIFY(!s.move);
            if(s.windows.contains({1}))QCOMPARE(s.windows.at({1}).geometry.position,original);
        }
    }
    void moveGrabRejectsOtherWindowsAndUnselectedParents()
    {
        auto s=initial();addApp(s,1,1);addApp(s,2,2);dispatch(s,ActivateWindow{{1}});
        auto bad=transition(s,{s.session,BeginWindowMove{{2}}});
        QCOMPARE(bad.rejection,Rejection::blocked_by_modal);QCOMPARE(bad.next,s);
        dispatch(s,BeginWindowMove{{1}});
        bad=transition(s,{s.session,UpdateWindowMove{{2},{300,300}}});
        QCOMPARE(bad.rejection,Rejection::stale_window_move);QCOMPARE(bad.next,s);
        dispatch(s,RegisterWindow{{3},{1},{{100,100},{200,100}},false,WindowId{1},true});
        QVERIFY(!s.move);
        bad=transition(s,{s.session,BeginWindowMove{{1}}});
        QCOMPARE(bad.rejection,Rejection::blocked_by_modal);
        dispatch(s,BeginWindowMove{{3}}); // The modal itself can still be moved.
        dispatch(s,RemoveApplication{{1}});QVERIFY(!s.move);
    }
    void desktopOwnsContextWithoutFocusingAnOldFolderWindow()
    {
        auto s = initial();
        dispatch(s, RegisterApplication{{1}, true});
        dispatch(s, RegisterWindow{{1}, {1}, {{0, 30}, {640, 480}}, true});
        dispatch(s, ActivateWindow{{1}});
        dispatch(s, ActivateDesktop{{1}});
        QVERIFY(s.active.desktop);
        QVERIFY(!s.active.window);
        QCOMPARE(desired_focus(s).kind, FocusKind::none);
        dispatch(s, BeginMenuTracking{{1}});
        QVERIFY(s.menu->captured_context.desktop);
        dispatch(s, EndMenuTracking{{1}});
        dispatch(s, ActivateApplication{{1}});
        QVERIFY(!s.active.desktop);
        QCOMPARE(s.active.window, std::optional<WindowId>{{1}});
        dispatch(s, RemoveWindow{{1}});
        QVERIFY(s.active.desktop);
    }
    void desktopOwnershipAndModalBlockingAreExplicit()
    {
        auto s = initial();
        dispatch(s, RegisterApplication{{1}, true});
        dispatch(s, RegisterApplication{{2}});
        auto t = transition(s, {s.session, RegisterApplication{{3}, true}});
        QCOMPARE(t.rejection, Rejection::invalid_relationship);
        QCOMPARE(t.next, s);
        t = transition(s, {s.session, ActivateDesktop{{2}}});
        QCOMPARE(t.rejection, Rejection::invalid_relationship);
        dispatch(s, ActivateDesktop{{1}});
        dispatch(s, RegisterWindow{{1}, {1}, {{0, 30}, {320, 200}}, false, {}, true});
        QVERIFY(!s.active.desktop);
        QCOMPARE(s.active.window, std::optional<WindowId>{{1}});
        t = transition(s, {s.session, ActivateDesktop{{1}}});
        QCOMPARE(t.rejection, Rejection::blocked_by_modal);
        QCOMPARE(t.next, s);
        dispatch(s, RemoveWindow{{1}});
        dispatch(s, ActivateDesktop{{1}});
        QVERIFY(s.active.desktop);
        dispatch(s, RegisterWindow{{2}, {1}, {{0, 30}, {320, 200}}, true});
        QVERIFY(s.active.desktop);
        dispatch(s, SetWindowRelationship{{2}, {}, true});
        QVERIFY(!s.active.desktop);
        QCOMPARE(s.active.window, std::optional<WindowId>{{2}});
        dispatch(s, RemoveWindow{{2}});
        QVERIFY(s.active.desktop);
    }
    void registrationDoesNotStealActivation()
    {
        auto s = initial();
        addApp(s, 1, 1);
        dispatch(s, ActivateApplication{{1}});
        addApp(s, 2, 2);
        QCOMPARE(s.active.application, std::optional<ApplicationId>{{1}});
        QCOMPARE(visible_order(s), (std::vector<WindowId>{{2}, {1}}));
    }
    void applicationLayerPreservesInternalOrder()
    {
        auto s = initial();
        addApp(s, 1, 1);
        addApp(s, 2, 2);
        dispatch(s, RegisterWindow{{3}, {1}, {{80, 90}, {200, 100}}, true});
        dispatch(s, ActivateWindow{{3}});
        QCOMPARE(visible_order(s), (std::vector<WindowId>{{2}, {1}, {3}}));
        dispatch(s, ActivateApplication{{2}});
        dispatch(s, ActivateApplication{{1}});
        QCOMPARE(visible_order(s), (std::vector<WindowId>{{2}, {1}, {3}}));
    }
    void collapseHideMoveRestore()
    {
        auto s = initial();
        addApp(s, 1, 1);
        addApp(s, 2, 2);
        dispatch(s, ActivateWindow{{1}});
        dispatch(s, SetCollapsed{{1}, true});
        QCOMPARE(desired_focus(s).kind, FocusKind::none);
        dispatch(s, SetPosition{{1}, {100, 140}});
        dispatch(s, HideApplication{{1}});
        QCOMPARE(s.active.application, std::optional<ApplicationId>{{2}});
        dispatch(s, ActivateApplication{{1}});
        QVERIFY(s.windows.at({1}).collapsed);
        dispatch(s, ActivateWindow{{1}, Expansion::restore});
        QVERIFY(!s.windows.at({1}).collapsed);
        QCOMPARE(s.windows.at({1}).geometry, (ContentGeometry{{100, 140}, {640, 480}}));
    }
    void menuContextAndStaleEnd()
    {
        auto s = initial();
        addApp(s, 1, 1);
        addApp(s, 2, 2);
        dispatch(s, ActivateWindow{{1}});
        auto context = s.active;
        dispatch(s, BeginMenuTracking{{1}});
        QCOMPARE(s.active, context);
        QCOMPARE(desired_focus(s).kind, FocusKind::menu);
        dispatch(s, ActivateApplication{{2}});
        QVERIFY(!s.menu);
        auto stale = transition(s, {s.session, EndMenuTracking{{1}}});
        QCOMPARE(stale.status, Status::rejected);
        QCOMPARE(stale.next, s);
        dispatch(s, BeginMenuTracking{{2}});
        QCOMPARE(s.menu->captured_context.application, std::optional<ApplicationId>{{2}});
    }
    void lastWindowDoesNotQuitApplication()
    {
        auto s = initial();
        addApp(s, 1, 1);
        dispatch(s, ActivateWindow{{1}});
        dispatch(s, BeginMenuTracking{{1}});
        dispatch(s, RemoveWindow{{1}});
        QVERIFY(s.applications.contains({1}));
        QVERIFY(!s.active.window);
        QVERIFY(!s.menu);
        QCOMPARE(s.active.application, std::optional<ApplicationId>{{1}});
        dispatch(s, BeginMenuTracking{{2}});
    }
    void identitiesAreNotReused()
    {
        auto s = initial();
        addApp(s, 1, 1);
        dispatch(s, RemoveWindow{{1}});
        auto t = transition(s, {s.session, RegisterWindow{{1}, {1}, {{0, 0}, {30, 30}}, true}});
        QCOMPARE(t.rejection, Rejection::reused_identity);
        QCOMPARE(t.next, s);
        dispatch(s, RemoveApplication{{1}});
        t = transition(s, {s.session, RegisterApplication{{1}}});
        QCOMPARE(t.rejection, Rejection::reused_identity);
    }
    void invalidInputsHaveNoEffects()
    {
        auto s = initial();
        addApp(s, 1, 1);
        for (const auto event : {Event{ObserveContentSize{{1}, {0, 480}}},
                                 Event{ActivateWindow{{99}}}, Event{RegisterApplication{{0}}}})
        {
            auto t = transition(s, {s.session, event});
            QCOMPARE(t.status, Status::rejected);
            QCOMPARE(t.next, s);
            QVERIFY(t.effects.empty());
        }
        auto t = transition(s, {{99}, ActivateWindow{{1}}});
        QCOMPARE(t.rejection, Rejection::wrong_session);
        QCOMPARE(t.next, s);
    }
    void unqualifiedShadeRejected()
    {
        auto s = initial();
        dispatch(s, RegisterApplication{{1}});
        dispatch(s, RegisterWindow{{1}, {1}, {{0, 0}, {640, 480}}, false});
        const auto t = transition(s, {s.session, SetCollapsed{{1}, true}});
        QCOMPARE(t.rejection, Rejection::shade_not_qualified);
        QCOMPARE(t.next, s);
    }
    void hiddenApplicationsLoseInput()
    {
        auto s = initial();
        addApp(s, 1, 1);
        addApp(s, 2, 2);
        dispatch(s, ActivateWindow{{1}});
        dispatch(s, HideApplication{{1}});
        dispatch(s, HideApplication{{2}});
        QVERIFY(visible_order(s).empty());
        QVERIFY(!s.active.application);
        QCOMPARE(desired_focus(s).kind, FocusKind::none);
    }
    void showAllRevealsWithoutStealingActivation()
    {
        auto s = initial();
        addApp(s, 1, 1);
        addApp(s, 2, 2);
        addApp(s, 3, 3);
        dispatch(s, ActivateWindow{{1}});
        dispatch(s, HideApplication{{2}});
        dispatch(s, HideApplication{{3}});
        dispatch(s, ShowApplication{{2}});
        QVERIFY(!s.applications.at({2}).hidden);
        QCOMPARE(s.active.application, std::optional<ApplicationId>{{1}});
        QCOMPARE(s.active.window, std::optional<WindowId>{{1}});
        const auto unchanged = transition(s, {s.session, ShowApplication{{2}}});
        QCOMPARE(unchanged.status, Status::no_change);
        QCOMPARE(transition(s, {s.session, ShowApplication{{9}}}).rejection, Rejection::unknown_application);
        // With everything hidden, the revealed application becomes the active one.
        dispatch(s, HideApplication{{1}});
        dispatch(s, HideApplication{{2}});
        QVERIFY(!s.active.application);
        dispatch(s, ShowApplication{{3}});
        QCOMPARE(s.active.application, std::optional<ApplicationId>{{3}});
    }
    void contentSizeObservationWhileCollapsed()
    {
        auto s = initial();
        addApp(s, 1, 1);
        dispatch(s, ActivateWindow{{1}});
        dispatch(s, SetCollapsed{{1}, true});
        dispatch(s, ObserveContentSize{{1}, {800, 600}});
        QVERIFY(s.windows.at({1}).collapsed);
        QCOMPARE(s.windows.at({1}).geometry.observed_size, (Size{800, 600}));
    }
    void corruptStateIsRejected()
    {
        auto s = initial();
        addApp(s, 1, 1);
        s.applications.at({1}).windows_back_to_front.push_back({1});
        const auto t = transition(s, {s.session, ActivateApplication{{1}}});
        QCOMPARE(t.rejection, Rejection::invalid_state);
        QVERIFY(t.effects.empty());
    }
    void modalDialogBlocksParentAndPreventsShading()
    {
        auto s = initial();
        addApp(s, 1, 1);
        dispatch(s, ActivateWindow{{1}});
        dispatch(s, RegisterWindow{{2}, {1}, {{80, 80}, {300, 150}}, false, WindowId{1}, true});
        dispatch(s, ActivateWindow{{1}});
        QCOMPARE(s.active.window, std::optional<WindowId>{{2}});
        auto t = transition(s, {s.session, SetCollapsed{{1}, true}});
        QCOMPARE(t.rejection, Rejection::blocked_by_modal);
        dispatch(s, RemoveWindow{{2}});
        QCOMPARE(s.active.window, std::optional<WindowId>{{1}});
    }
    void transientRelationshipCannotCycle()
    {
        auto s = initial();
        addApp(s, 1, 1);
        dispatch(s, RegisterWindow{{2}, {1}, {{0, 0}, {200, 100}}, false, WindowId{1}, false});
        auto t = transition(s, {s.session, SetWindowRelationship{{1}, WindowId{2}, false}});
        QCOMPARE(t.rejection, Rejection::invalid_relationship);
        QCOMPARE(t.next, s);
        dispatch(s, RemoveWindow{{1}});
        QVERIFY(!s.windows.at({2}).parent);
    }
};
QTEST_APPLESS_MAIN(PolicyTests)
#include "policy_tests.moc"
