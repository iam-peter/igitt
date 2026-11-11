# igitt's Qt Quick Controls style, the only one it uses. It comes from the
# Trainingsplaner's, which started out after shadcn/ui
# (https://ui.shadcn.com). A QML module whose files are named after the
# controls; what it leaves out comes from Basic. Its colors come from
# theme files (themes/, and the user's own), see docs/THEMES.md.
set(style_singletons
    Primitives.qml TokenInterface.qml ButtonStyle.qml FieldStyle.qml ToggleStyle.qml ItemStyle.qml
    TabStyle.qml PopupStyle.qml ScrollBarStyle.qml ProgressBarStyle.qml ControlState.qml GroupStyle.qml
)
set_source_files_properties(${style_singletons} PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_library(igitt_style STATIC)
qt_add_qml_module(igitt_style
    URI IgittStyle
    VERSION 1.0
    IMPORTS QtQuick.Controls.Basic
    # Style and StyleCascade: the rules the control styles are made of;
    # Themes: the colors
    SOURCES
        stylevalues.h stylevalues.cpp
        style.h style.cpp
        stylecascade.h stylecascade.cpp
        themelibrary.h themelibrary.cpp
        themes.h themes.cpp
    QML_FILES
        ${style_singletons}
        Semantics.qml
        StyledBackground.qml
        ButtonLabel.qml
        Shadow.qml
        ApplicationWindow.qml
        Button.qml
        CheckBox.qml
        ComboBox.qml
        Dialog.qml
        DialogButtonBox.qml
        Frame.qml
        ItemDelegate.qml
        Label.qml
        Menu.qml
        MenuItem.qml
        MenuSeparator.qml
        Page.qml
        Pane.qml
        Popup.qml
        ProgressBar.qml
        RadioButton.qml
        ScrollBar.qml
        ScrollIndicator.qml
        SpinBox.qml
        Switch.qml
        TabBar.qml
        TabButton.qml
        TextArea.qml
        TextField.qml
        ToolBar.qml
        ToolButton.qml
        ToolSeparator.qml
        ToolTip.qml
    RESOURCES
        images/check.svg
        images/chevron-down.svg
        images/chevron-right.svg
        themes/light.json
        themes/dark.json
        themes/alucard.json
        themes/dracula.json
        themes/catppuccin-latte.json
        themes/catppuccin-frappe.json
        themes/catppuccin-macchiato.json
        themes/catppuccin-mocha.json
)
target_include_directories(igitt_style PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(igitt_style PUBLIC Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2)
