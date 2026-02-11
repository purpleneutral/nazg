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

#include "brain/pattern_matcher.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace nazg::brain {

PatternMatcher::PatternMatcher(nexus::Store *store, blackbox::logger *log)
    : store_(store), log_(log) {}

std::vector<PatternMatcher::Pattern>
PatternMatcher::find_matching_patterns(int64_t project_id,
                                        const std::string &error_signature,
                                        const std::string &failure_type,
                                        double /*similarity_threshold*/,
                                        const std::string &error_message) {
  std::vector<Pattern> matches;

  if (!store_) {
    return matches;
  }

  // Get all patterns for this project and type
  auto all_patterns = list_patterns(project_id, 1000);

  for (const auto &pattern : all_patterns) {
    // Filter by type if specified
    if (!failure_type.empty() && pattern.failure_type != failure_type) {
      continue;
    }

    // Check signature match (exact match is 1.0)
    if (pattern.signature == error_signature) {
      matches.push_back(pattern);
      continue;
    }

    // If pattern has regex and we have an error message, check regex match
    if (!pattern.error_regex.empty() && !error_message.empty() &&
        matches_error_pattern(error_message, pattern.error_regex)) {
      matches.push_back(pattern);
      continue;
    }
  }

  return matches;
}

int64_t PatternMatcher::learn_pattern(
    int64_t project_id, const std::vector<int64_t> &similar_failure_ids,
    const std::string &pattern_name) {

  if (!store_ || similar_failure_ids.empty()) {
    return 0;
  }

  // Get the first failure to extract signature and type
  auto first_failure = store_->get_failure(similar_failure_ids[0]);
  if (!first_failure) {
    return 0;
  }

  std::string error_signature = first_failure->at("error_signature");
  std::string failure_type = first_failure->at("failure_type");

  // Check if pattern already exists
  auto existing = get_pattern_by_signature(project_id, error_signature);
  if (existing) {
    // Update existing pattern
    if (log_) {
      log_->info("Brain", "Updating existing pattern #" +
                              std::to_string(existing->id));
    }
    update_pattern_stats(existing->id);
    return existing->id;
  }

  // Generate pattern name if not provided
  std::string name = pattern_name;
  if (name.empty()) {
    // Collect error messages
    std::vector<std::string> error_messages;
    for (auto fid : similar_failure_ids) {
      auto failure = store_->get_failure(fid);
      if (failure) {
        error_messages.push_back(failure->at("error_message"));
      }
    }
    name = generate_pattern_name(error_messages, failure_type);
  }

  // Create error regex pattern (simplified for now)
  std::string error_regex = extract_error_pattern(first_failure->at("error_message"));

  // Empty trigger conditions for now
  std::string trigger_conditions = "{}";

  // Create pattern
  int64_t pattern_id =
      store_->record_pattern(project_id, error_signature, name, failure_type,
                             error_regex, trigger_conditions);

  if (pattern_id > 0) {
    // Link all failures to this pattern
    for (auto fid : similar_failure_ids) {
      link_failure_to_pattern(fid, pattern_id);
    }

    if (log_) {
      log_->info("Brain", "Created pattern #" + std::to_string(pattern_id) +
                              ": " + name + " (from " +
                              std::to_string(similar_failure_ids.size()) +
                              " failures)");
    }
  }

  return pattern_id;
}

std::vector<PatternMatcher::SimilarFailure>
PatternMatcher::find_similar_failures(int64_t project_id,
                                       const std::string &error_signature,
                                       const std::string &error_message,
                                       int limit) {
  std::vector<SimilarFailure> similar;

  if (!store_) {
    return similar;
  }

  // First, get exact signature matches
  auto exact_matches = store_->find_similar_failures(project_id, error_signature, limit * 2);

  for (const auto &match : exact_matches) {
    SimilarFailure sim;
    sim.failure_id = std::stoll(match.at("id"));
    sim.error_signature = match.at("error_signature");
    sim.error_message = match.at("error_message");
    sim.timestamp = std::stoll(match.at("timestamp"));
    sim.was_resolved = match.at("resolved") == "1";
    sim.resolution_type = match.at("resolution_type");

    // Compute similarity score
    sim.similarity_score = compute_similarity(error_message, sim.error_message);

    // Only include if similarity is above threshold
    if (sim.similarity_score >= 0.5) {
      similar.push_back(sim);
    }
  }

  // Sort by similarity score (highest first)
  std::sort(similar.begin(), similar.end(),
            [](const SimilarFailure &a, const SimilarFailure &b) {
              return a.similarity_score > b.similarity_score;
            });

  // Limit results
  if (similar.size() > static_cast<size_t>(limit)) {
    similar.resize(limit);
  }

  return similar;
}

double PatternMatcher::compute_similarity(const std::string &error_a,
                                           const std::string &error_b) {
  // Use a combination of Levenshtein and token-based similarity

  // Levenshtein similarity (character-level)
  double lev_sim = levenshtein_similarity(error_a, error_b);

  // Token similarity (word-level)
  double tok_sim = token_similarity(error_a, error_b);

  // Weighted average (70% token, 30% Levenshtein)
  return 0.7 * tok_sim + 0.3 * lev_sim;
}

std::optional<PatternMatcher::Pattern>
PatternMatcher::get_pattern(int64_t pattern_id) {
  if (!store_) {
    return std::nullopt;
  }

  auto row = store_->get_pattern(pattern_id);
  if (!row) {
    return std::nullopt;
  }

  return parse_pattern(*row);
}

std::optional<PatternMatcher::Pattern>
PatternMatcher::get_pattern_by_signature(int64_t project_id,
                                          const std::string &signature) {
  if (!store_) {
    return std::nullopt;
  }

  auto row = store_->get_pattern_by_signature(project_id, signature);
  if (!row) {
    return std::nullopt;
  }

  return parse_pattern(*row);
}

std::vector<PatternMatcher::Pattern>
PatternMatcher::list_patterns(int64_t project_id, int limit) {
  std::vector<Pattern> patterns;

  if (!store_) {
    return patterns;
  }

  auto rows = store_->list_patterns(project_id, limit);
  for (const auto &row : rows) {
    patterns.push_back(parse_pattern(row));
  }

  return patterns;
}

bool PatternMatcher::update_pattern_stats(int64_t pattern_id) {
  if (!store_) {
    return false;
  }
  return store_->update_pattern_statistics(pattern_id, 1);
}

bool PatternMatcher::link_failure_to_pattern(int64_t failure_id,
                                              int64_t pattern_id) {
  if (!store_) {
    return false;
  }
  return store_->link_failure_to_pattern(failure_id, pattern_id);
}

// ===== Private Helper Methods =====

int PatternMatcher::levenshtein_distance(const std::string &s1,
                                          const std::string &s2) {
  const size_t len1 = s1.size();
  const size_t len2 = s2.size();

  // Use a single vector for space efficiency
  std::vector<int> prev(len2 + 1);
  std::vector<int> curr(len2 + 1);

  // Initialize first row
  for (size_t j = 0; j <= len2; ++j) {
    prev[j] = static_cast<int>(j);
  }

  for (size_t i = 1; i <= len1; ++i) {
    curr[0] = static_cast<int>(i);

    for (size_t j = 1; j <= len2; ++j) {
      int cost = (s1[i - 1] == s2[j - 1]) ? 0 : 1;

      curr[j] = std::min({prev[j] + 1,         // deletion
                          curr[j - 1] + 1,     // insertion
                          prev[j - 1] + cost}); // substitution
    }

    std::swap(prev, curr);
  }

  return prev[len2];
}

double PatternMatcher::levenshtein_similarity(const std::string &a,
                                               const std::string &b) {
  if (a.empty() && b.empty()) {
    return 1.0;
  }

  if (a.empty() || b.empty()) {
    return 0.0;
  }

  int distance = levenshtein_distance(a, b);
  int max_len = std::max(a.length(), b.length());

  // Convert distance to similarity (0.0 to 1.0)
  return 1.0 - (static_cast<double>(distance) / max_len);
}

std::vector<std::string>
PatternMatcher::tokenize_error(const std::string &error) {
  std::vector<std::string> tokens;
  std::istringstream iss(error);
  std::string word;

  while (iss >> word) {
    // Convert to lowercase
    std::transform(word.begin(), word.end(), word.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    // Remove punctuation
    word.erase(std::remove_if(word.begin(), word.end(),
                              [](unsigned char c) { return std::ispunct(c); }),
               word.end());

    // Skip very short words and numbers
    if (word.length() > 2 && !std::all_of(word.begin(), word.end(), ::isdigit)) {
      tokens.push_back(word);
    }
  }

  return tokens;
}

double PatternMatcher::token_similarity(const std::string &error_a,
                                         const std::string &error_b) {
  auto tokens_a = tokenize_error(error_a);
  auto tokens_b = tokenize_error(error_b);

  if (tokens_a.empty() && tokens_b.empty()) {
    return 1.0;
  }

  if (tokens_a.empty() || tokens_b.empty()) {
    return 0.0;
  }

  // Compute Jaccard similarity: |intersection| / |union|
  std::unordered_set<std::string> set_a(tokens_a.begin(), tokens_a.end());
  std::unordered_set<std::string> set_b(tokens_b.begin(), tokens_b.end());

  // Intersection
  std::vector<std::string> intersection;
  for (const auto &token : set_a) {
    if (set_b.count(token) > 0) {
      intersection.push_back(token);
    }
  }

  // Union size = |A| + |B| - |intersection|
  size_t union_size = set_a.size() + set_b.size() - intersection.size();

  return static_cast<double>(intersection.size()) / union_size;
}

std::string
PatternMatcher::extract_error_pattern(const std::string &error_message) {
  // Extract key error phrases for pattern matching
  std::string pattern = error_message;

  // Take first few lines only
  std::istringstream iss(pattern);
  std::string line;
  std::ostringstream result;
  int line_count = 0;

  while (std::getline(iss, line) && line_count < 3) {
    if (!line.empty()) {
      result << line << "\n";
      line_count++;
    }
  }

  return result.str();
}

bool PatternMatcher::matches_error_pattern(const std::string &error,
                                            const std::string &pattern_regex) {
  if (pattern_regex.empty()) {
    return false;
  }

  try {
    std::regex re(pattern_regex, std::regex::icase);
    return std::regex_search(error, re);
  } catch (const std::regex_error &) {
    // Invalid regex
    return false;
  }
}

std::string PatternMatcher::generate_pattern_name(
    const std::vector<std::string> &error_messages,
    const std::string &failure_type) {

  if (error_messages.empty()) {
    return failure_type + " Pattern";
  }

  // Look for common keywords in error messages
  std::string first_error = error_messages[0];
  std::string lower_error = first_error;
  std::transform(lower_error.begin(), lower_error.end(), lower_error.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  // Common error patterns
  if (lower_error.find("undefined reference") != std::string::npos) {
    return "Undefined Reference";
  } else if (lower_error.find("no such file") != std::string::npos) {
    return "Missing File";
  } else if (lower_error.find("cannot find") != std::string::npos) {
    return "Missing Dependency";
  } else if (lower_error.find("permission denied") != std::string::npos) {
    return "Permission Error";
  } else if (lower_error.find("segmentation fault") != std::string::npos ||
             lower_error.find("sigsegv") != std::string::npos) {
    return "Segmentation Fault";
  } else if (lower_error.find("assertion") != std::string::npos ||
             lower_error.find("assert") != std::string::npos) {
    return "Assertion Failure";
  } else if (lower_error.find("timeout") != std::string::npos) {
    return "Timeout Error";
  } else if (lower_error.find("out of memory") != std::string::npos ||
             lower_error.find("oom") != std::string::npos) {
    return "Out of Memory";
  }

  // Extract first meaningful word from error
  std::istringstream iss(first_error);
  std::string word;
  std::vector<std::string> words;
  while (iss >> word && words.size() < 3) {
    // Skip common words
    std::string lower_word = word;
    std::transform(lower_word.begin(), lower_word.end(), lower_word.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower_word != "error" && lower_word != "error:" &&
        lower_word != "fatal" && lower_word != "warning" && word.length() > 3) {
      words.push_back(word);
    }
  }

  if (!words.empty()) {
    std::ostringstream name;
    for (size_t i = 0; i < words.size(); ++i) {
      if (i > 0)
        name << " ";
      name << words[i];
    }
    return name.str();
  }

  return failure_type + " Pattern";
}

PatternMatcher::Pattern
PatternMatcher::parse_pattern(const std::map<std::string, std::string> &row) {
  Pattern pattern;
  pattern.id = std::stoll(row.at("id"));
  pattern.signature = row.at("pattern_signature");
  pattern.name = row.at("pattern_name");
  pattern.failure_type = row.at("failure_type");

  if (row.count("error_regex")) {
    pattern.error_regex = row.at("error_regex");
  }

  pattern.occurrence_count = std::stoi(row.at("occurrence_count"));
  pattern.first_seen = std::stoll(row.at("first_seen"));
  pattern.last_seen = std::stoll(row.at("last_seen"));

  if (row.count("trigger_conditions_json")) {
    pattern.trigger_conditions_json = row.at("trigger_conditions_json");
  }

  if (row.count("resolution_strategies_json")) {
    pattern.resolution_strategies_json = row.at("resolution_strategies_json");
  }

  if (row.count("failure_ids_json")) {
    pattern.failure_ids = parse_failure_ids_json(row.at("failure_ids_json"));
  }

  return pattern;
}

std::vector<int64_t>
PatternMatcher::parse_failure_ids_json(const std::string &json) {
  std::vector<int64_t> ids;

  if (json.empty() || json == "[]") {
    return ids;
  }

  // Simple JSON array parser: [123, 456, 789]
  std::string numbers = json;
  // Remove brackets
  numbers.erase(std::remove(numbers.begin(), numbers.end(), '['), numbers.end());
  numbers.erase(std::remove(numbers.begin(), numbers.end(), ']'), numbers.end());

  std::istringstream iss(numbers);
  std::string id_str;
  while (std::getline(iss, id_str, ',')) {
    // Trim whitespace
    id_str.erase(0, id_str.find_first_not_of(" \t\n\r"));
    id_str.erase(id_str.find_last_not_of(" \t\n\r") + 1);

    if (!id_str.empty()) {
      ids.push_back(std::stoll(id_str));
    }
  }

  return ids;
}

std::string
PatternMatcher::failure_ids_to_json(const std::vector<int64_t> &ids) {
  if (ids.empty()) {
    return "[]";
  }

  std::ostringstream json;
  json << "[";
  for (size_t i = 0; i < ids.size(); ++i) {
    if (i > 0)
      json << ",";
    json << ids[i];
  }
  json << "]";
  return json.str();
}

} // namespace nazg::brain
