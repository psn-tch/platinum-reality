#include "emulator_launch.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>

bool adaptKnownEmulatorLaunch(frankenstein::applications::LaunchPlan &plan, QString &error)
{
    error.clear();
    if (QFileInfo(plan.program).fileName() != "ymir-compatible.sh") return true;
    const QByteArray known =
        "#!/usr/bin/env bash\nset -euo pipefail\n"
        "base=\"$(cd -- \"$(dirname -- \"${BASH_SOURCE[0]}\")\" && pwd)\"\n"
        "export SDL_VIDEODRIVER=x11\nexport SDL_RENDER_DRIVER=opengl\n"
        "exec /usr/bin/ymir-sdl3 --profile \"$base/Ymir-compatible-profile\" \"$@\"\n";
    QFile source(plan.program);
    if (!source.open(QIODevice::ReadOnly) || source.size() != known.size() || source.readAll() != known)
        return true; // A customized wrapper retains its own semantics.
    const auto profile=QFileInfo(plan.program).absoluteDir().filePath("Ymir-compatible-profile");
    if (!QFileInfo(profile).isDir() || !QFileInfo("/usr/bin/ymir-sdl3").isExecutable()) {
        error="Ymir's existing compatibility profile or executable is unavailable.";
        return false;
    }
    plan.program="/usr/bin/ymir-sdl3";
    plan.arguments=QStringList{"--profile",QFileInfo(profile).canonicalFilePath()}+plan.arguments;
    plan.existingApplicationSettings=true;
    return true;
}

void configureKnownEmulatorEnvironment(const QString &program, QProcessEnvironment &environment)
{
    if (QFileInfo(program).canonicalFilePath() != "/usr/bin/ymir-sdl3") return;
    environment.insert("SDL_VIDEODRIVER","wayland");
    environment.insert("SDL_RENDER_DRIVER","opengl,software");
}
