#pragma once

namespace uniforge::platform {

// True when Windows "Choose your app mode" is set to Dark
// (HKCU\...\Themes\Personalize\AppsUseLightTheme == 0). Light when the value is missing.
[[nodiscard]] bool systemPrefersDarkApps();

} // namespace uniforge::platform
