# Soulu Beta 1.0.52 — toolbar and window interaction fixes

Removed the lower toolbar divider and the separator before window controls. Main and Classic toolbar actions now share button and icon geometry, including hidden VPN/download actions, address actions and the Tab Overview button. Icons remain centered through hover, pressed and disabled states.

Minimize and Maximize/Restore use quiet circular neutral feedback; Close uses a soft red circle. Caption glyphs stay fixed in size and position, while the full Windows click targets are preserved.

Fixed Site Info and nested panel positioning by expanding the native shell before showing or focusing a panel. Permissions, Reader appearance and Find remain bounded within the client area, with scrolling for tall content. Closing another surface no longer collapses an open panel.

Restored dragging from free title areas in Classic Mode and kept drag exclusions current across layout switches. Existing tabs, navigation, Settings, Home, History, Onest typography and motion are retained.

CEF 154.0.33+ga03e714 / Chromium 154.0.8037.94 remain unchanged.
