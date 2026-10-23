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
} // namespace
class PolicyTests : public QObject
{
    Q_OBJECT
  private slots:
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
