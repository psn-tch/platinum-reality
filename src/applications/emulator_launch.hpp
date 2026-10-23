#pragma once
#include "desktop_entry.hpp"
#include <QProcessEnvironment>

// Recognize only the exact earlier X11 compatibility wrapper. Preserve its
// explicit sibling profile; never rewrite the user's launcher or configuration.
bool adaptKnownEmulatorLaunch(frankenstein::applications::LaunchPlan &plan, QString &error);

// Display settings for a directly launched Ymir: native Wayland (this desktop
// has no X11), and the OpenGL renderer the user's wrapper chose, falling back to
// SDL's software renderer if GPU buffers are unavailable in this session.
void configureKnownEmulatorEnvironment(const QString &program, QProcessEnvironment &environment);
