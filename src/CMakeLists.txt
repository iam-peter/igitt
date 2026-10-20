# Backend: git access, parsers and the models exposed to QML. Kept free of
# any UI so the tests can link it directly.
qt_add_library(igitt_core STATIC)
qt_add_qml_module(igitt_core
    URI Igitt.Core
    VERSION 1.0
    SOURCES
        core/GitRunner.h core/GitRunner.cpp
        core/LogParser.h core/LogParser.cpp
        core/GraphLayout.h core/GraphLayout.cpp
        core/StatusParser.h core/StatusParser.cpp
        core/DiffParser.h core/DiffParser.cpp
        core/Highlighter.h core/Highlighter.cpp
        core/WordDiff.h core/WordDiff.cpp
        core/DiffMarkup.h core/DiffMarkup.cpp
        core/PatchBuilder.h core/PatchBuilder.cpp
        core/FoldEngine.h core/FoldEngine.cpp
        core/FoldSuggester.h core/FoldSuggester.cpp
        core/StateLoader.h core/StateLoader.cpp
        core/PlanSession.h core/PlanSession.cpp
        core/MessageFormat.h core/MessageFormat.cpp
        core/FileHistory.h core/FileHistory.cpp
        core/MergeConflict.h core/MergeConflict.cpp
        core/RemoteOps.h core/RemoteOps.cpp
        core/GerritReview.h core/GerritReview.cpp
        models/PlanModel.h models/PlanModel.cpp
        models/Timelapse.h models/Timelapse.cpp
        models/ConflictResolver.h models/ConflictResolver.cpp
        models/FileTreeModel.h models/FileTreeModel.cpp
        models/CommitModel.h models/CommitModel.cpp
        models/FileListModel.h models/FileListModel.cpp
        models/DiffModel.h models/DiffModel.cpp
        Repository.h Repository.cpp
)
# The generated QML type registrations include headers by file name only.
target_include_directories(igitt_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/models ${CMAKE_CURRENT_SOURCE_DIR}/core
)
target_link_libraries(igitt_core PUBLIC Qt6::Core Qt6::Qml)

# KDE's highlighter knows hundreds of languages; without it a built-in one
# covers the few igitt's own sources are written in.
option(IGITT_USE_KSYNTAXHIGHLIGHTING "Highlight syntax with KSyntaxHighlighting if it is found" ON)
if(IGITT_USE_KSYNTAXHIGHLIGHTING)
    find_package(KF6SyntaxHighlighting QUIET)
endif()
if(KF6SyntaxHighlighting_FOUND)
    message(STATUS "Highlighting syntax with KSyntaxHighlighting ${KF6SyntaxHighlighting_VERSION}")
    target_link_libraries(igitt_core PRIVATE KF6::SyntaxHighlighting)
    target_compile_definitions(igitt_core PRIVATE IGITT_HAVE_KSYNTAXHIGHLIGHTING)
else()
    message(STATUS "KSyntaxHighlighting not found; using the built-in highlighter")
endif()

# UI: QML and the custom items, as a library so the UI smoke test can load it.
set_source_files_properties(qml/AppSettings.qml qml/Icons.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_library(igitt_ui STATIC)
qt_add_qml_module(igitt_ui
    URI Igitt
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/HistoryView.qml
        qml/CommitDelegate.qml
        qml/RefLabel.qml
        qml/ChangesPane.qml
        qml/FileList.qml
        qml/DiffView.qml
        qml/PlanPane.qml
        qml/WipRow.qml
        qml/Tag.qml
        qml/CommitBox.qml
        qml/SettingsDialog.qml
        qml/AppSettings.qml
        qml/TimelapseWindow.qml
        qml/ConflictView.qml
        qml/Icons.qml
        qml/IconButton.qml
        qml/Icon.qml
        qml/ModalDialog.qml
        qml/FilesPane.qml
        qml/BoundedScrollView.qml
        qml/SplitterHandle.qml
    SOURCES
        ui/CommitGraph.h ui/CommitGraph.cpp
        ui/DiffLineText.h ui/DiffLineText.cpp
        ui/Clipboard.h
        ui/ActivityMinimap.h ui/ActivityMinimap.cpp
        ui/Fonts.h ui/Fonts.cpp
        ui/RevisionTimeline.h ui/RevisionTimeline.cpp
    RESOURCES
        icons/case-sensitive.svg
        icons/check.svg
        icons/chevron-down.svg
        icons/chevron-right.svg
        icons/chevron-up.svg
        icons/chevrons-down-up.svg
        icons/chevrons-up-down.svg
        icons/circle-alert.svg
        icons/circle-check.svg
        icons/cloud-download.svg
        icons/cloud-upload.svg
        icons/columns-3.svg
        icons/copy.svg
        icons/file.svg
        icons/files.svg
        icons/fold-vertical.svg
        icons/folder-open.svg
        icons/folder-tree.svg
        icons/folder.svg
        icons/git-branch.svg
        icons/git-commit-horizontal.svg
        icons/git-merge.svg
        icons/git-pull-request.svg
        icons/grip-vertical.svg
        icons/highlighter.svg
        icons/history.svg
        icons/list-checks.svg
        icons/pause.svg
        icons/play.svg
        icons/plus.svg
        icons/refresh-cw.svg
        icons/regex.svg
        icons/search.svg
        icons/settings.svg
        icons/skip-back.svg
        icons/skip-forward.svg
        icons/space.svg
        icons/step-back.svg
        icons/step-forward.svg
        icons/tag.svg
        icons/undo-2.svg
        icons/whole-word.svg
        icons/x.svg
        fonts/Inter-Bold.ttf
        fonts/Inter-Italic.ttf
        fonts/Inter-Medium.ttf
        fonts/Inter-Regular.ttf
        fonts/Inter-SemiBold.ttf
        fonts/JetBrainsMono-Bold.ttf
        fonts/JetBrainsMono-Italic.ttf
        fonts/JetBrainsMono-Regular.ttf
)
target_include_directories(igitt_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/ui)
target_link_libraries(igitt_ui PUBLIC
    igitt_core
    igitt_coreplugin
    Qt6::Gui
    Qt6::Quick
    Qt6::QuickControls2
)

qt_add_executable(igitt main.cpp)
target_compile_definitions(igitt PRIVATE PROJECT_VERSION="${PROJECT_VERSION}")
target_link_libraries(igitt PRIVATE igitt_ui igitt_uiplugin)

install(TARGETS igitt RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
