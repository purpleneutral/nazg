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
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nazg::nexus {
class Store;
}

namespace nazg::blackbox {
class logger;
}

namespace nazg::brain {

// Pattern Recognition and Similarity Matching
// Identifies repeated failure patterns and computes similarity scores
class PatternMatcher {
public:
  PatternMatcher(nexus::Store *store, blackbox::logger *log);

  // Pattern information
  struct Pattern {
    int64_t id;
    std::string signature;
    std::string name;
    std::string failure_type;
    std::string error_regex;

    // Statistics
    int occurrence_count;
    int64_t first_seen;
    int64_t last_seen;

    // Related failures (IDs)
    std::vector<int64_t> failure_ids;

    // JSON strings for additional data
    std::string trigger_conditions_json;
    std::string resolution_strategies_json;
  };

  // Similar failure information
  struct SimilarFailure {
    int64_t failure_id;
    double similarity_score; // 0.0 to 1.0
    std::string error_message;
    std::string error_signature;
    int64_t timestamp;
    bool was_resolved;
    std::string resolution_type;
  };

  // Find matching patterns for a failure
  std::vector<Pattern> find_matching_patterns(int64_t project_id,
                                               const std::string &error_signature,
                                               const std::string &failure_type,
                                               double similarity_threshold = 0.7,
                                               const std::string &error_message = "");

  // Create or update pattern from similar failures
  // If pattern exists, updates it; otherwise creates new one
  int64_t learn_pattern(int64_t project_id,
                        const std::vector<int64_t> &similar_failure_ids,
                        const std::string &pattern_name = "");

  // Find similar past failures
  std::vector<SimilarFailure> find_similar_failures(int64_t project_id,
                                                     const std::string &error_signature,
                                                     const std::string &error_message,
                                                     int limit = 10);

  // Compute similarity between two error messages
  // Returns score from 0.0 (completely different) to 1.0 (identical)
  double compute_similarity(const std::string &error_a,
                            const std::string &error_b);

  // Get a specific pattern by ID
  std::optional<Pattern> get_pattern(int64_t pattern_id);

  // Get pattern by signature
  std::optional<Pattern> get_pattern_by_signature(int64_t project_id,
                                                   const std::string &signature);

  // List all patterns for a project
  std::vector<Pattern> list_patterns(int64_t project_id, int limit = 100);

  // Update pattern statistics (increment occurrence count)
  bool update_pattern_stats(int64_t pattern_id);

  // Link a failure to an existing pattern
  bool link_failure_to_pattern(int64_t failure_id, int64_t pattern_id);

private:
  nexus::Store *store_;
  blackbox::logger *log_;

  // Similarity scoring helpers

  // Levenshtein distance between two strings
  int levenshtein_distance(const std::string &s1, const std::string &s2);

  // Compute similarity using Levenshtein distance (0.0 to 1.0)
  double levenshtein_similarity(const std::string &a, const std::string &b);

  // Tokenize error message into meaningful parts
  std::vector<std::string> tokenize_error(const std::string &error);

  // Compute token-based similarity (Jaccard similarity)
  double token_similarity(const std::string &error_a,
                          const std::string &error_b);

  // Extract error pattern (normalized key parts)
  std::string extract_error_pattern(const std::string &error_message);

  // Check if error matches a regex pattern
  bool matches_error_pattern(const std::string &error,
                              const std::string &pattern_regex);

  // Generate a human-readable pattern name from error messages
  std::string generate_pattern_name(const std::vector<std::string> &error_messages,
                                     const std::string &failure_type);

  // Parse pattern data from database row
  Pattern parse_pattern(const std::map<std::string, std::string> &row);

  // Parse JSON array of failure IDs
  std::vector<int64_t> parse_failure_ids_json(const std::string &json);

  // Convert failure IDs to JSON string
  std::string failure_ids_to_json(const std::vector<int64_t> &ids);
};

} // namespace nazg::brain
