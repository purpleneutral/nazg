// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 purpleneutral
//
// This file is part of nazg.
//
// nazg is free software: you can redistribute it and/or modify it under
// the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// nazg is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along
// with nazg. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <memory>
#include <vector>

namespace nazg::tui {

class Pane;
class Window;

/**
 * @brief Rectangle representing a pane's position and size
 */
struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  Rect() = default;
  Rect(int x, int y, int w, int h) : x(x), y(y), width(w), height(h) {}
};

/**
 * @brief Split direction for panes
 */
enum class SplitDirection {
  HORIZONTAL,  // Split left/right
  VERTICAL     // Split top/bottom
};

/**
 * @brief Layout engine manages pane arrangement using a binary tree
 *
 * Each node is either:
 * - A leaf containing a Pane pointer
 * - A split node with two children and a split direction
 */
class Layout {
public:
  Layout();
  Layout(Layout&&) noexcept = default;
  Layout& operator=(Layout&&) noexcept = default;
  ~Layout();

  /**
   * @brief Set the root pane (initial state)
   */
  void set_root_pane(Pane* pane);

  /**
   * @brief Split a pane in the specified direction
   * @param target_pane The pane to split
   * @param new_pane The new pane to add
   * @param direction Split direction
   * @return true if split succeeded
   */
  bool split_pane(Pane* target_pane, Pane* new_pane, SplitDirection direction);

  /**
   * @brief Remove a pane from the layout
   * @param pane The pane to remove
   * @return true if pane was removed
   */
  bool remove_pane(Pane* pane);

  /**
   * @brief Calculate positions for all panes
   * @param area The total area to fill
   * @return Vector of (Pane*, Rect) pairs
   */
  std::vector<std::pair<Pane*, Rect>> calculate_positions(const Rect& area) const;

  /**
   * @brief Get list of all panes in the layout
   */
  std::vector<Pane*> get_all_panes() const;

  /**
   * @brief Get pane in a direction relative to current pane
   * @param current Current pane
   * @param direction Navigation direction ('h', 'j', 'k', 'l')
   * @return Next pane in that direction, or nullptr
   */
  Pane* get_pane_in_direction(Pane* current, char direction) const;

  struct Snapshot {
    struct Node {
      bool is_leaf = true;
      SplitDirection split_dir = SplitDirection::HORIZONTAL;
      float split_ratio = 0.5f;
      size_t leaf_index = 0;
      std::unique_ptr<Node> left;
      std::unique_ptr<Node> right;
    };

    std::unique_ptr<Node> root;
    size_t leaf_count = 0;

    bool empty() const { return !root || leaf_count == 0; }
  };

  Snapshot snapshot() const;
  bool restore_from_snapshot(const Snapshot& snapshot, const std::vector<Pane*>& leaves);

private:
  friend class Window;

  struct Node {
    // Leaf node
    Pane* pane = nullptr;

    // Split node
    SplitDirection split_dir = SplitDirection::HORIZONTAL;
    std::unique_ptr<Node> left;
    std::unique_ptr<Node> right;
    float split_ratio = 0.5f; // 0.0-1.0, position of split

    bool is_leaf() const { return pane != nullptr; }
  };

  std::unique_ptr<Node> root_;

  const Node* root() const { return root_.get(); }
  Node* root() { return root_.get(); }

  /**
   * @brief Find the node containing target_pane
   */
  Node* find_node(Node* node, Pane* target_pane) const;

  /**
   * @brief Recursively calculate positions
   */
  void calculate_positions_recursive(Node* node, const Rect& area,
                                     std::vector<std::pair<Pane*, Rect>>& result) const;

  /**
   * @brief Collect all panes from tree
   */
  void collect_panes(Node* node, std::vector<Pane*>& result) const;

  /**
   * @brief Get pane by position (for directional navigation)
   */
  Pane* get_pane_at_position(int x, int y, const Rect& area) const;
};

} // namespace nazg::tui
