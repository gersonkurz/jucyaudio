# Folder Navigation Design Document (Revision 3)

## 1. Executive Summary

This document outlines the design for implementing hierarchical folder navigation within JucyAudio's `DataViewComponent`. The core principle of this design is a **Node-Centric Command Architecture**. The UI (`DataViewComponent`) is treated as a "dumb" renderer that forwards user interaction events to the active `INavigationNode`. The Node, in turn, provides the UI with *semantic hints* for rendering and *actionable intents* for user interactions.

All presentation, navigation, and action logic resides within the node itself, ensuring a clean separation of concerns and maximizing the abstraction of the core logic from the UI layer. This approach not only enables the desired folder navigation but also represents a significant architectural improvement over the existing data presentation model, paying down technical debt and creating a more robust and maintainable system.

## 2. Architectural Philosophy: The Node is the Controller

This architecture enforces a strict **Model-View** pattern for the navigation system.

*   **The View (`DataViewComponent`):**
    *   Its sole responsibility is to render what the Node tells it to render by interpreting semantic hints through a theme engine.
    *   It does not understand the *type* of data it is displaying (e.g., track, folder).
    *   It forwards all user actions (like double-clicks) to the Node as abstract "activation" events.

*   **The Model/Controller (`INavigationNode`):**
    *   It is the single source of truth for its content.
    *   It provides all necessary information for rendering via semantic states (e.g., "this text is important," "this text is secondary").
    *   It contains all logic for handling user actions. When an "activation" event is received, the node returns an *intent* (e.g., "play this track," "navigate to this new node"), which the UI then orchestrates.

This philosophy ensures that all application logic remains within the core `database` library, making the UI layer exceptionally simple, stable, and independent of the data's meaning.

## 3. The New `INavigationNode` Interface

To support this new architecture, the `INavigationNode` interface will be extended with two new methods, which will eventually replace several legacy methods.

#### `getCellRenderInfo`: The Visualization Contract

This method provides the View with semantic hints for rendering. The UI's theme engine is responsible for translating these hints into specific colors and fonts.

```cpp
// In a shared header
// Semantic states for a row or cell. The UI theme maps these to actual styles.
enum class RenderState {
    Normal,     // Default text for standard items like track titles
    Accent,     // Important information, such as a folder name or a selected item
    Subdued,    // Secondary information, like a track count or file format
    Inactive    // Offline or otherwise unavailable content
};

struct CellRenderInfo {
    std::string text;
    RenderState state = RenderState::Normal;
};

// In INavigationNode.h
virtual CellRenderInfo getCellRenderInfo(RowIndex_t rowIndex, ColumnIndex_t columnIndex) const = 0;
```
**Rationale for Changes:**
*   This is a superior abstraction to providing direct styles. The Node reports the *semantic meaning* (`Accent`, `Subdued`), and the UI's theme engine decides how to represent that. This fully decouples the core logic from presentation details.
*   By removing `isBold` and `iconId`, we adhere to the YAGNI principle. Icons can be efficiently included in the `text` string via Unicode symbols.

#### `onRowActivated`: The User-Action Contract

This method informs the UI of the *intent* of a user action. The UI is then responsible for orchestrating the components needed to fulfill that intent (e.g., telling the player to load a track).

```cpp
// In a shared header
enum class RowActivationResultType {
    NoAction,
    NavigateToNode,
    PlayTrack
};

struct RowActivationResult {
    RowActivationResultType type = RowActivationResultType::NoAction;
    
    // Valid if type is NavigateToNode.
    // Ownership: Node returns a retained pointer; caller must release.
    INavigationNode* newNode = nullptr; 
    
    // Valid if type is PlayTrack.
    std::optional<TrackId> trackToPlay;
};

// In INavigationNode.h
virtual RowActivationResult onRowActivated(RowIndex_t rowIndex) = 0;
```
**Rationale for Changes:**
*   The `PlayTrack` intent is specific and actionable. The Node's responsibility ends at identifying *which* track should be played.
*   The UI (`MainComponent`) receives the `TrackId` and orchestrates the subsequent actions (telling the `EnhancedPlayer` to load the track, triggering waveform analysis, etc.), which is the correct separation of concerns.

## 4. Phased Implementation Plan

This refactoring will be executed in carefully managed phases to ensure the application remains stable and functional throughout the process.

### Phase 1: Evolve the Core Interface & Create a Translation Layer

**Objective:** Introduce the new interface methods without breaking existing code.

1.  **Define Structs:** Add `RenderState`, `CellRenderInfo`, `RowActivationResultType`, and `RowActivationResult` to a common header.
2.  **Extend Interface:** Add `getCellRenderInfo` and `onRowActivated` to `INavigationNode`.
3.  **Implement Default Translators:** In `BaseNode`, provide default `virtual` implementations for the new methods. These will call the old interface methods (`getCellText`, `getTrackInfoForRow`) and translate their output. For example, `getCellRenderInfo` will return `{ getCellText(...), RenderState::Normal }`.

### Phase 2: Migrate the UI (`DataViewComponent`)

**Objective:** Update the UI to exclusively use the new, cleaner interface methods.

1.  **Refactor `paintCell`:** Modify the cell painting logic. It will call `m_currentNode->getCellRenderInfo()` and use a `switch` on the returned `RenderState` to look up the appropriate color and font from the application's theme manager.
2.  **Refactor `cellDoubleClicked`:** Modify the double-click handler to call `m_currentNode->onRowActivated()`. The handler will contain a simple `switch` on the result `type` to either call `m_mainComponent.navigateToNode()` or `m_mainComponent.playTrack()`. All complex logic is removed from the UI.

### Phase 3: Migrate Existing Nodes

**Objective:** Incrementally update each concrete node class (`WorkingSetNode`, `MixNode`, etc.) to natively support the new interface.

1.  For each node, override `getCellRenderInfo` and `onRowActivated` to provide semantic render states and actionable play intents directly, without relying on the old methods.

### Phase 4: Implement Folder Navigation

**Objective:** With the new, robust architecture in place, implement the original feature request.

1.  **Modify `VirtualFolderNode`:**
    *   **`getCellRenderInfo`:** The logic will check a user setting (`hierarchicalFolderView`). If hierarchical, it will determine if a `rowIndex` is a ".." navigator, a folder, or a track, returning a `CellRenderInfo` with the appropriate `RenderState` (`Accent` for folders, `Subdued` for counts, etc.).
    *   **`onRowActivated`:** The logic will determine the row type. It will return a `RowActivationResult` of type `NavigateToNode` for ".." and folder rows (creating a new node for children), or `PlayTrack` for track rows.
2.  **Implement in Other Folder Nodes:** Apply similar logic to `LogicalFolderNode` and `VirtualFoldersOverview`.

### Phase 5: Deprecation and Cleanup

**Objective:** Finalize the refactor by removing all legacy code.

1.  **Audit:** Once all nodes are migrated and folder navigation is fully functional, confirm that the old methods are no longer called.
2.  **Remove Legacy Methods:** Delete `getCellText`, `getTrackInfoForRow`, `getObjectIdForRow`, and any other obsolete methods from the entire class hierarchy.
3.  **Enforce Contract:** Change `getCellRenderInfo` and `onRowActivated` back to pure virtual (`= 0`) in the `INavigationNode` interface to ensure all future nodes must conform to the new architecture.

## 5. Benefits of this Approach

1.  **Superior Abstraction:** Fully decouples the core logic from UI presentation details and themes.
2.  **Increased Maintainability:** Drastically simplifies the `DataViewComponent` and centralizes logic within the relevant nodes.
3.  **Low-Risk Implementation:** The phased approach ensures the application is always in a working state.
4.  **Architectural Improvement:** The refactoring itself is a valuable outcome, resulting in a cleaner, more robust, and more theme-friendly system for the future.