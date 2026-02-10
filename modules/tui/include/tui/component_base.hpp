#pragma once

#include "tui/theme.hpp"
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <memory>
#include <string>
#include <vector>

namespace nazg::tui {

/**
 * @brief Component type classification
 */
enum class ComponentType {
  CONTAINER,  // Has children (Box, Split, Tabs)
  LEAF        // No children (MenuList, Text, Input)
};

/**
 * @brief Base interface for all TUI components
 *
 * Components are the building blocks of menus. They can be composed in a
 * parent-child hierarchy to create complex layouts. Components handle their
 * own rendering and event processing.
 *
 * Design principles:
 * - Composable: Components can contain other components
 * - Declarative: Use builder pattern for intuitive API
 * - Event-driven: Components handle events and bubble up if not handled
 * - Flexible layout: Support fixed, constrained, flex, and auto sizing
 */
class ComponentBase {
public:
  virtual ~ComponentBase() = default;

  // ============================================================================
  // Identity
  // ============================================================================

  /**
   * @brief Get unique component identifier
   */
  virtual std::string id() const = 0;

  /**
   * @brief Get component type (CONTAINER or LEAF)
   */
  virtual ComponentType type() const = 0;

  // ============================================================================
  // Hierarchy Management
  // ============================================================================

  /**
   * @brief Add a child component
   * @param child Child to add (ownership transferred)
   */
  virtual void add_child(std::unique_ptr<ComponentBase> child);

  /**
   * @brief Remove a child component by ID
   * @param child_id ID of child to remove
   * @return true if child was removed
   */
  virtual bool remove_child(const std::string& child_id);

  /**
   * @brief Get all child components
   */
  virtual const std::vector<std::unique_ptr<ComponentBase>>& children() const {
    return children_;
  }

  /**
   * @brief Get parent component (nullptr if root)
   */
  ComponentBase* parent() const { return parent_; }

  // ============================================================================
  // Lifecycle
  // ============================================================================

  /**
   * @brief Called when component is added to the component tree
   */
  virtual void on_mount() {}

  /**
   * @brief Called when component is removed from the component tree
   */
  virtual void on_unmount() {}

  // ============================================================================
  // Focus Management
  // ============================================================================

  /**
   * @brief Called when component receives focus
   */
  virtual void on_focus() { focused_ = true; }

  /**
   * @brief Called when component loses focus
   */
  virtual void on_blur() { focused_ = false; }

  /**
   * @brief Check if component is currently focused
   */
  virtual bool is_focused() const { return focused_; }

  /**
   * @brief Check if component can receive focus
   * @return true if component accepts keyboard focus
   */
  virtual bool is_focusable() const { return false; }

  // ============================================================================
  // Event Handling
  // ============================================================================

  /**
   * @brief Handle an input event
   * @param event The event to handle
   * @return true if event was handled, false to bubble up to parent
   */
  virtual bool handle_event(const ftxui::Event& event) { return false; }

  // ============================================================================
  // Rendering
  // ============================================================================

  /**
   * @brief Render the component
   * @param width Available width
   * @param height Available height
   * @param theme Color theme to use
   * @return FTXUI element to display
   */
  virtual ftxui::Element render(int width, int height, const Theme& theme) = 0;

  // ============================================================================
  // Layout Hints
  // ============================================================================

  /**
   * @brief Layout hints for parent containers
   *
   * Describes how this component wants to be sized within its parent.
   * Parents use these hints to calculate child sizes.
   */
  struct LayoutHints {
    // Fixed sizing (takes priority if set)
    int fixed_width = -1;   // -1 = not set
    int fixed_height = -1;  // -1 = not set

    // Constraints (honored if fixed not set)
    int min_width = 0;
    int min_height = 0;
    int max_width = -1;  // -1 = unlimited
    int max_height = -1;

    // Flex factor (used when distributing remaining space)
    // Higher values get more space. Default 1.0 means equal distribution.
    float flex_grow = 1.0f;

    // Auto sizing (component calculates optimal size)
    bool auto_width = false;
    bool auto_height = false;
  };

  /**
   * @brief Get layout hints for this component
   */
  virtual LayoutHints get_layout_hints() const { return layout_hints_; }

  // ============================================================================
  // Async Loading Support
  // ============================================================================

  /**
   * @brief Check if component is currently loading data
   * @return true if loading, false if ready
   */
  virtual bool is_loading() const { return false; }

  /**
   * @brief Get loading message to display
   */
  virtual std::string loading_message() const { return "Loading..."; }

protected:
  ComponentBase* parent_ = nullptr;
  bool focused_ = false;
  std::vector<std::unique_ptr<ComponentBase>> children_;
  LayoutHints layout_hints_;

  /**
   * @brief Set parent (called by add_child)
   */
  void set_parent(ComponentBase* parent) { parent_ = parent; }

};

} // namespace nazg::tui
