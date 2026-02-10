#include "brain/learner.hpp"
#include "brain/pattern_matcher.hpp"
#include "blackbox/logger.hpp"
#include "nexus/store.hpp"
#include <algorithm>
#include <cctype>
#include <regex>
#include <sstream>

namespace nazg::brain {

Learner::Learner(nexus::Store *store, workspace::Manager *workspace_mgr,
                 blackbox::logger *log)
    : store_(store), workspace_mgr_(workspace_mgr), log_(log),
      pattern_matcher_(new PatternMatcher(store, log)) {}

Learner::~Learner() {
  delete pattern_matcher_;
}

int64_t Learner::record_failure(int64_t project_id,
                                 const FailureContext &context) {
  if (!store_) {
    if (log_) {
      log_->error("Brain", "Cannot record failure: no store");
    }
    return 0;
  }

  // Compute error signature
  std::string error_signature = compute_error_signature(context.error_message);

  // Convert context data to JSON strings
  std::string changed_files_json = changed_files_to_json(context.changed_files);
  std::string changed_deps_json = changed_deps_to_json(context.changed_deps);
  std::string changed_env_json = changed_env_to_json(context.changed_env);
  std::string changed_system_json = changed_env_to_json(context.changed_system);
  std::string tags_json = tags_to_json(context.tags);

  // Record failure with full context
  int64_t failure_id = store_->record_failure_with_context(
      project_id, context.failure_type, error_signature, context.error_message,
      context.error_location, context.command, context.exit_code,
      context.before_snapshot_id, context.after_snapshot_id, changed_files_json,
      changed_deps_json, changed_env_json, changed_system_json,
      context.severity, tags_json);

  if (failure_id > 0 && log_) {
    log_->info("Brain",
               "Recorded failure #" + std::to_string(failure_id) + " (" +
                   context.failure_type + ")");
  }

  // Auto-pattern learning: Check for similar failures
  if (failure_id > 0 && pattern_matcher_) {
    auto similar = pattern_matcher_->find_similar_failures(
        project_id, error_signature, context.error_message, 10);

    // If we have 3+ similar failures (including this one), create/update pattern
    if (similar.size() >= 3) {
      // Collect failure IDs
      std::vector<int64_t> failure_ids;
      for (const auto &sim : similar) {
        failure_ids.push_back(sim.failure_id);
      }

      // Check if pattern already exists
      auto patterns = pattern_matcher_->find_matching_patterns(
          project_id, error_signature, context.failure_type, 0.8);

      if (patterns.empty()) {
        // Create new pattern
        int64_t pattern_id = pattern_matcher_->learn_pattern(
            project_id, failure_ids, "");

        if (pattern_id > 0 && log_) {
          log_->info("Brain",
                     "🧠 Learned new pattern #" + std::to_string(pattern_id) +
                         " from " + std::to_string(similar.size()) +
                         " similar failures");
        }
      } else {
        // Update existing pattern
        pattern_matcher_->update_pattern_stats(patterns[0].id);
        pattern_matcher_->link_failure_to_pattern(failure_id, patterns[0].id);

        if (log_) {
          log_->info("Brain",
                     "📊 Linked failure to existing pattern #" +
                         std::to_string(patterns[0].id) + ": " +
                         patterns[0].name);
        }
      }
    }
  }

  return failure_id;
}

bool Learner::mark_resolved(int64_t failure_id,
                             const std::string &resolution_type,
                             int64_t resolution_snapshot_id,
                             const std::string &notes, bool success) {
  if (!store_) {
    if (log_) {
      log_->error("Brain", "Cannot mark resolved: no store");
    }
    return false;
  }

  bool ok = store_->update_failure_resolution(failure_id, resolution_type,
                                               resolution_snapshot_id, notes,
                                               success);

  if (ok && log_) {
    log_->info("Brain",
               "Marked failure #" + std::to_string(failure_id) + " as " +
                   (success ? "successfully" : "unsuccessfully") +
                   " resolved via " + resolution_type);
  }

  return ok;
}

std::optional<Learner::FailureContext>
Learner::detect_failure(const std::string &command, const std::string &output,
                        int exit_code) {
  // Only detect failures for non-zero exit codes
  if (exit_code == 0) {
    return std::nullopt;
  }

  FailureContext context;
  context.command = command;
  context.error_message = output;
  context.exit_code = exit_code;

  // Infer failure type from command and output
  context.failure_type = infer_failure_type(command, output);

  // Extract error location if present
  context.error_location = extract_error_location(output);

  // Determine severity based on error type and message
  if (output.find("error:") != std::string::npos ||
      output.find("ERROR") != std::string::npos ||
      output.find("fatal") != std::string::npos) {
    context.severity = "high";
  } else if (output.find("warning") != std::string::npos ||
             output.find("WARNING") != std::string::npos) {
    context.severity = "low";
  } else {
    context.severity = "medium";
  }

  return context;
}

Learner::FailureStats Learner::get_statistics(int64_t project_id) {
  FailureStats stats;

  if (!store_) {
    return stats;
  }

  auto failures = store_->list_failures(project_id, 1000);
  stats.total_failures = static_cast<int>(failures.size());

  for (const auto &failure : failures) {
    // Count by resolution status
    bool resolved = failure.at("resolved") == "1";
    if (!resolved) {
      stats.unresolved_failures++;
    } else {
      std::string res_type = failure.at("resolution_type");
      if (res_type == "auto") {
        stats.auto_resolved++;
      } else {
        stats.manually_resolved++;
      }
    }

    // Count by type
    std::string type = failure.at("failure_type");
    stats.by_type[type]++;

    // Count by severity
    std::string severity = failure.at("severity");
    stats.by_severity[severity]++;
  }

  return stats;
}

std::optional<std::map<std::string, std::string>>
Learner::get_failure(int64_t failure_id) {
  if (!store_) {
    return std::nullopt;
  }
  return store_->get_failure(failure_id);
}

std::vector<std::map<std::string, std::string>>
Learner::list_failures(int64_t project_id, int limit) {
  if (!store_) {
    return {};
  }
  return store_->list_failures(project_id, limit);
}

std::vector<std::map<std::string, std::string>>
Learner::list_unresolved_failures(int64_t project_id, int limit) {
  if (!store_) {
    return {};
  }
  return store_->list_unresolved_failures(project_id, limit);
}

std::vector<std::map<std::string, std::string>>
Learner::find_similar_failures(int64_t project_id,
                                const std::string &error_signature, int limit) {
  if (!store_) {
    return {};
  }
  return store_->find_similar_failures(project_id, error_signature, limit);
}

// ===== Helper Methods =====

std::string Learner::compute_error_signature(const std::string &error_message) {
  // Extract the core error message
  std::string core = extract_error_core(error_message);

  // Normalize it
  std::string normalized = normalize_error_message(core);

  // Compute a simple hash (in production, use proper hash like SHA-256)
  size_t hash = 0;
  for (char c : normalized) {
    hash = hash * 31 + c;
  }

  std::ostringstream ss;
  ss << std::hex << hash;
  return ss.str();
}

std::string
Learner::normalize_error_message(const std::string &raw_error) {
  std::string normalized = raw_error;

  // Remove absolute paths - replace with relative markers
  std::regex path_regex(R"(/[^\s:]+/)");
  normalized = std::regex_replace(normalized, path_regex, "<PATH>/");

  // Remove line numbers
  std::regex line_regex(R"(:(\d+):)");
  normalized = std::regex_replace(normalized, line_regex, ":<LINE>:");

  // Remove hex addresses
  std::regex hex_regex(R"(0x[0-9a-fA-F]+)");
  normalized = std::regex_replace(normalized, hex_regex, "<HEX>");

  // Remove timestamps
  std::regex time_regex(
      R"(\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}(\.\d+)?(Z|[+-]\d{2}:\d{2})?)");
  normalized = std::regex_replace(normalized, time_regex, "<TIMESTAMP>");

  // Normalize whitespace
  std::regex ws_regex(R"(\s+)");
  normalized = std::regex_replace(normalized, ws_regex, " ");

  // Convert to lowercase for case-insensitive matching
  std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  return normalized;
}

std::string
Learner::extract_error_location(const std::string &error_message) {
  // Look for common patterns: file.ext:line:col or file.ext:line
  std::regex location_regex(R"(([^\s:]+\.(cpp|hpp|h|c|py|js|ts|rs|go)):(\d+):?(\d+)?)");
  std::smatch match;

  std::string searchable = error_message;
  if (std::regex_search(searchable, match, location_regex)) {
    return match[1].str() + ":" + match[3].str();
  }

  return "";
}

std::string Learner::infer_failure_type(const std::string &command,
                                         const std::string &error) {
  // Check command keywords
  std::string cmd_lower = command;
  std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  if (cmd_lower.find("test") != std::string::npos ||
      cmd_lower.find("pytest") != std::string::npos ||
      cmd_lower.find("jest") != std::string::npos ||
      cmd_lower.find("cargo test") != std::string::npos) {
    return "test";
  }

  if (cmd_lower.find("cmake") != std::string::npos ||
      cmd_lower.find("make") != std::string::npos ||
      cmd_lower.find("cargo build") != std::string::npos ||
      cmd_lower.find("npm run build") != std::string::npos) {
    // Check if it's a link error
    if (error.find("undefined reference") != std::string::npos ||
        error.find("unresolved external") != std::string::npos ||
        error.find("ld:") != std::string::npos ||
        error.find("linker") != std::string::npos) {
      return "link";
    }
    return "build";
  }

  // Check error message for clues
  if (error.find("undefined reference") != std::string::npos ||
      error.find("unresolved external") != std::string::npos) {
    return "link";
  }

  if (error.find("error:") != std::string::npos ||
      error.find("compilation") != std::string::npos) {
    return "build";
  }

  if (error.find("assertion") != std::string::npos ||
      error.find("FAILED") != std::string::npos) {
    return "test";
  }

  // Default to build if uncertain
  return "build";
}

std::string Learner::extract_error_core(const std::string &error_message) {
  // Extract the most relevant error lines
  // Look for lines with "error:", "Error:", "ERROR", "fatal"
  std::istringstream iss(error_message);
  std::string line;
  std::vector<std::string> error_lines;

  while (std::getline(iss, line)) {
    std::string line_lower = line;
    std::transform(line_lower.begin(), line_lower.end(), line_lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (line_lower.find("error:") != std::string::npos ||
        line_lower.find("fatal:") != std::string::npos ||
        line_lower.find("undefined reference") != std::string::npos ||
        line_lower.find("cannot find") != std::string::npos ||
        line_lower.find("no such file") != std::string::npos) {
      error_lines.push_back(line);
      if (error_lines.size() >= 3) {
        break; // Keep only first 3 error lines
      }
    }
  }

  if (error_lines.empty()) {
    // If no specific error lines found, take first few lines
    std::istringstream iss2(error_message);
    int count = 0;
    while (std::getline(iss2, line) && count < 3) {
      if (!line.empty()) {
        error_lines.push_back(line);
        count++;
      }
    }
  }

  // Join error lines
  std::ostringstream result;
  for (size_t i = 0; i < error_lines.size(); ++i) {
    if (i > 0)
      result << "\n";
    result << error_lines[i];
  }

  return result.str();
}

std::string
Learner::changed_files_to_json(const std::vector<std::string> &files) {
  if (files.empty()) {
    return "[]";
  }

  std::ostringstream json;
  json << "[";
  for (size_t i = 0; i < files.size(); ++i) {
    if (i > 0)
      json << ",";
    json << "\"" << files[i] << "\"";
  }
  json << "]";
  return json.str();
}

std::string Learner::changed_deps_to_json(
    const std::map<std::string, std::string> &deps) {
  if (deps.empty()) {
    return "{}";
  }

  std::ostringstream json;
  json << "{";
  bool first = true;
  for (const auto &kv : deps) {
    if (!first)
      json << ",";
    json << "\"" << kv.first << "\":\"" << kv.second << "\"";
    first = false;
  }
  json << "}";
  return json.str();
}

std::string Learner::changed_env_to_json(
    const std::map<std::string, std::string> &env) {
  if (env.empty()) {
    return "{}";
  }

  std::ostringstream json;
  json << "{";
  bool first = true;
  for (const auto &kv : env) {
    if (!first)
      json << ",";
    json << "\"" << kv.first << "\":\"" << kv.second << "\"";
    first = false;
  }
  json << "}";
  return json.str();
}

std::string Learner::tags_to_json(const std::vector<std::string> &tags) {
  if (tags.empty()) {
    return "[]";
  }

  std::ostringstream json;
  json << "[";
  for (size_t i = 0; i < tags.size(); ++i) {
    if (i > 0)
      json << ",";
    json << "\"" << tags[i] << "\"";
  }
  json << "]";
  return json.str();
}

PatternMatcher *Learner::pattern_matcher() { return pattern_matcher_; }

} // namespace nazg::brain
