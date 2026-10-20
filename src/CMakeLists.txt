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
        core/PatchBuilder.h core/PatchBuilder.cpp
        core/FoldEngine.h core/FoldEngine.cpp
        core/FoldSuggester.h core/FoldSuggester.cpp
        models/CommitModel.h models/CommitModel.cpp
        models/FileListModel.h models/FileListModel.cpp
        models/DiffModel.h models/DiffModel.cpp
        Repository.h Repository.cpp
)
# The generated QML type registrations include headers by file name only.
target_include_directories(igitt_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/models
)
target_link_libraries(igitt_core PUBLIC Qt6::Core Qt6::Qml)

# UI: QML and the custom items, as a library so the UI smoke test can load it.
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
    SOURCES
        ui/CommitGraph.h ui/CommitGraph.cpp
)
target_include_directories(igitt_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui)
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
