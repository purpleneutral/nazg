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

#include "tui/managers/layout.hpp"
#include "tui/pane.hpp"
#include <algorithm>
#include <functional>
#include <climits>
#include <vector>


namespace nazg::tui {

Layout::Layout() = default;
Layout::~Layout() = default;

void Layout::set_root_pane(Pane* pane) {
  root_ = std::make_unique<Node>();
  root_->pane = pane;
}

bool Layout::split_pane(Pane* target_pane, Pane* new_pane, SplitDirection direction) {
  if (!root_) {
    return false;
  }

  Node* target_node = find_node(root_.get(), target_pane);
  if (!target_node || !target_node->is_leaf()) {
    return false;
  }

  // Convert leaf node to split node
  auto old_left = std::make_unique<Node>();
  old_left->pane = target_node->pane;

  auto new_right = std::make_unique<Node>();
  new_right->pane = new_pane;

  target_node->pane = nullptr; // No longer a leaf
  target_node->split_dir = direction;
  target_node->split_ratio = 0.5f;
  target_node->left = std::move(old_left);
  target_node->right = std::move(new_right);

  return true;
}

bool Layout::remove_pane(Pane* pane) {
  if (!root_) {
    return false;
  }

  // Special case: removing the only pane
  if (root_->is_leaf() && root_->pane == pane) {
    root_.reset();
    return true;
  }

  // Find parent of node containing pane
  std::function<Node*(Node*, Pane*)> find_parent = [&](Node* node, Pane* target) -> Node* {
    if (!node || node->is_leaf()) {
      return nullptr;
    }

    // Check if either child is the target
    if (node->left && node->left->is_leaf() && node->left->pane == target) {
      return node;
    }
    if (node->right && node->right->is_leaf() && node->right->pane == target) {
      return node;
    }

    // Recurse
    if (auto* result = find_parent(node->left.get(), target)) {
      return result;
    }
    return find_parent(node->right.get(), target);
  };

  Node* parent = find_parent(root_.get(), pane);
  if (!parent) {
    return false;
  }

  // Replace parent with the sibling of the removed pane
  bool removed_left = parent->left && parent->left->is_leaf() && parent->left->pane == pane;
  std::unique_ptr<Node>& promote_ptr = removed_left ? parent->right : parent->left;

  if (!promote_ptr) {
    return false;
  }


  // Move the sibling node into the parent in one shot to preserve children
  *parent = std::move(*promote_ptr);
  promote_ptr.reset();

  return true;
}

Layout::Snapshot Layout::snapshot() const {
  Snapshot snap;

  std::function<std::unique_ptr<Snapshot::Node>(const Node*, size_t&)> clone =
      [&](const Node* node, size_t& leaf_counter) -> std::unique_ptr<Snapshot::Node> {
        if (!node) {
          return nullptr;
        }

        auto snap_node = std::make_unique<Snapshot::Node>();
        if (node->is_leaf()) {
          snap_node->is_leaf = true;
          snap_node->leaf_index = leaf_counter++;
        } else {
          snap_node->is_leaf = false;
          snap_node->split_dir = node->split_dir;
          snap_node->split_ratio = node->split_ratio;
          snap_node->left = clone(node->left.get(), leaf_counter);
          snap_node->right = clone(node->right.get(), leaf_counter);
        }
        return snap_node;
      };

  size_t leaf_counter = 0;
  snap.root = clone(root_.get(), leaf_counter);
  snap.leaf_count = leaf_counter;
  return snap;
}

bool Layout::restore_from_snapshot(const Snapshot& snapshot, const std::vector<Pane*>& leaves) {
  if (!snapshot.root) {
    root_.reset();
    return leaves.empty();
  }

  if (snapshot.leaf_count != leaves.size()) {
    return false;
  }

  std::function<std::unique_ptr<Node>(const Snapshot::Node*)> build =
      [&](const Snapshot::Node* snap_node) -> std::unique_ptr<Node> {
        if (!snap_node) {
          return nullptr;
        }

        auto node = std::make_unique<Node>();
        if (snap_node->is_leaf) {
          if (snap_node->leaf_index >= leaves.size()) {
            return nullptr;
          }
          node->pane = leaves[snap_node->leaf_index];
        } else {
          node->pane = nullptr;
          node->split_dir = snap_node->split_dir;
          node->split_ratio = snap_node->split_ratio;
          node->left = build(snap_node->left.get());
          node->right = build(snap_node->right.get());
          if (!node->left || !node->right) {
            return nullptr;
          }
        }
        return node;
      };

  auto new_root = build(snapshot.root.get());
  if (!new_root) {
    return false;
  }

  root_ = std::move(new_root);
  return true;
}

std::vector<std::pair<Pane*, Rect>> Layout::calculate_positions(const Rect& area) const {
  std::vector<std::pair<Pane*, Rect>> result;
  if (root_) {
    calculate_positions_recursive(root_.get(), area, result);
  }
  return result;
}

void Layout::calculate_positions_recursive(Node* node, const Rect& area,
                                           std::vector<std::pair<Pane*, Rect>>& result) const {
  if (!node) {
    return;
  }

  if (node->is_leaf()) {
    result.emplace_back(node->pane, area);
    return;
  }

  // Split the area
  Rect left_area = area;
  Rect right_area = area;

  if (node->split_dir == SplitDirection::HORIZONTAL) {
    // Split left/right
    int split_x = area.x + static_cast<int>(area.width * node->split_ratio);
    left_area.width = split_x - area.x;
    right_area.x = split_x;
    right_area.width = area.width - left_area.width;
  } else {
    // Split top/bottom
    int split_y = area.y + static_cast<int>(area.height * node->split_ratio);
    left_area.height = split_y - area.y;
    right_area.y = split_y;
    right_area.height = area.height - left_area.height;
  }

  calculate_positions_recursive(node->left.get(), left_area, result);
  calculate_positions_recursive(node->right.get(), right_area, result);
}

std::vector<Pane*> Layout::get_all_panes() const {
  std::vector<Pane*> result;
  if (root_) {
    collect_panes(root_.get(), result);
  }
  return result;
}

void Layout::collect_panes(Node* node, std::vector<Pane*>& result) const {
  if (!node) {
    return;
  }

  if (node->is_leaf()) {
    result.push_back(node->pane);
  } else {
    collect_panes(node->left.get(), result);
    collect_panes(node->right.get(), result);
  }
}

Pane* Layout::get_pane_in_direction(Pane* current, char direction) const {
  if (!root_) {
    return nullptr;
  }

  // Get current pane's rect
  auto positions = calculate_positions(Rect(0, 0, 100, 100)); // Dummy area
  auto current_it = std::find_if(positions.begin(), positions.end(),
                                 [current](const auto& p) { return p.first == current; });
  if (current_it == positions.end()) {
    return nullptr;
  }

  Rect current_rect = current_it->second;
  int center_x = current_rect.x + current_rect.width / 2;
  int center_y = current_rect.y + current_rect.height / 2;

  // Find best pane in direction
  Pane* best = nullptr;
  int best_distance = INT_MAX;

  for (const auto& [pane, rect] : positions) {
    if (pane == current) continue;

    int pane_center_x = rect.x + rect.width / 2;
    int pane_center_y = rect.y + rect.height / 2;
    int dx = pane_center_x - center_x;
    int dy = pane_center_y - center_y;

    bool correct_direction = false;
    int distance = 0;

    switch (direction) {
      case 'h': // left
        correct_direction = dx < 0;
        distance = -dx + std::abs(dy);
        break;
      case 'l': // right
        correct_direction = dx > 0;
        distance = dx + std::abs(dy);
        break;
      case 'k': // up
        correct_direction = dy < 0;
        distance = -dy + std::abs(dx);
        break;
      case 'j': // down
        correct_direction = dy > 0;
        distance = dy + std::abs(dx);
        break;
    }

    if (correct_direction && distance < best_distance) {
      best = pane;
      best_distance = distance;
    }
  }

  return best;
}

Layout::Node* Layout::find_node(Node* node, Pane* target_pane) const {
  if (!node) {
    return nullptr;
  }

  if (node->is_leaf() && node->pane == target_pane) {
    return node;
  }

  if (auto* result = find_node(node->left.get(), target_pane)) {
    return result;
  }

  return find_node(node->right.get(), target_pane);
}

Pane* Layout::get_pane_at_position(int x, int y, const Rect& area) const {
  auto positions = calculate_positions(area);
  for (const auto& [pane, rect] : positions) {
    if (x >= rect.x && x < rect.x + rect.width &&
        y >= rect.y && y < rect.y + rect.height) {
      return pane;
    }
  }
  return nullptr;
}

} // namespace nazg::tui
