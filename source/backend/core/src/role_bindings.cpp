#include "v4l2diag/core/role_bindings.hpp"

#include <algorithm>
#include <map>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace v4l2diag {

std::string slave_role(std::size_t index) {
  // 1-based: slaves[0] is "slave-1". The number follows the slaves[] order, not a
  // device path, so a camera moving to a different node keeps its role.
  return "slave-" + std::to_string(index + 1);
}

bool is_canonical_role(const std::string &role) {
  if (role == kMasterRole()) {
    return true;
  }
  const std::string prefix = "slave-";
  if (role.size() <= prefix.size() || role.compare(0, prefix.size(), prefix) != 0) {
    return false;
  }
  const std::string number = role.substr(prefix.size());
  // No leading zeros and no "slave-0": slave_role() is 1-based, so those names can
  // never be produced and accepting them would let two spellings mean one role.
  if (number[0] == '0') {
    return false;
  }
  return std::all_of(number.begin(), number.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
}

std::vector<std::string> expected_roles(std::size_t slave_count) {
  std::vector<std::string> roles;
  roles.reserve(slave_count + 1);
  roles.push_back(kMasterRole());
  for (std::size_t i = 0; i < slave_count; i++) {
    roles.push_back(slave_role(i));
  }
  return roles;
}

RoleResolution resolve_role_bindings(const std::vector<RoleBinding> &bindings,
                                     const std::vector<std::string> &channel_ids, std::size_t slave_count) {
  RoleResolution out;
  const std::vector<std::string> expected = expected_roles(slave_count);
  const std::set<std::string> expected_set(expected.begin(), expected.end());
  const std::set<std::string> known_channels(channel_ids.begin(), channel_ids.end());

  // Validation and resolution run off ONE index, so they cannot disagree: the map
  // that decides "duplicate" and "missing" is the same map the resolved output is
  // built from. An earlier version searched `bindings` again at the end and leaned
  // on an assert to say the search could not fail -- which is no protection at all
  // in a release build, in a function that takes external input.
  std::map<std::string, std::string> by_role;

  // Every fault kind is collected rather than returned at the first one, so a
  // caller fixing a routing does not need one round trip per mistake.
  std::set<std::string> reported_duplicates;
  for (const auto &binding : bindings) {
    const auto inserted = by_role.emplace(binding.role, binding.trigger_channel_id);
    if (!inserted.second) {
      // Second sighting of this role. The first binding stays in the map; it is not
      // used either way, since a duplicate fails the whole resolution.
      if (reported_duplicates.insert(binding.role).second) {
        out.duplicate_roles.push_back(binding.role);
      }
    } else if (expected_set.count(binding.role) == 0) {
      // Not a role this run has a camera for. Free-text names land here too: the
      // role set is closed, so an unrecognised name is a mistake, not a new role.
      out.unexpected_roles.push_back(binding.role);
    }
    if (known_channels.count(binding.trigger_channel_id) == 0) {
      out.unknown_channels.push_back(binding.trigger_channel_id);
    }
  }

  for (const auto &role : expected) {
    if (by_role.count(role) == 0) {
      // Never inferred, not even when the profile defines exactly one channel:
      // routing a trigger to the wrong camera is worse than asking the user to
      // fill in a field.
      out.missing_roles.push_back(role);
    }
  }

  if (!out.ok()) {
    // Deliberately no partial routing: a caller that ignored the error must not
    // find something plausible to fire with.
    return out;
  }
  // Output order follows the topology, not the input order.
  for (const auto &role : expected) {
    const auto it = by_role.find(role);
    if (it == by_role.end()) {
      // Unreachable while the checks above hold -- the missing-role loop just read
      // this same map. Handled rather than asserted anyway: if that invariant ever
      // breaks, a caller gets a failed resolution instead of undefined behaviour.
      out.resolved.clear();
      out.missing_roles.push_back(role);
      return out;
    }
    RoleBinding resolved;
    resolved.role = it->first;
    resolved.trigger_channel_id = it->second;
    out.resolved.push_back(resolved);
  }
  return out;
}

namespace {

void append_clause(std::string *out, const std::string &label, const std::vector<std::string> &values) {
  if (values.empty()) {
    return;
  }
  if (!out->empty()) {
    *out += "; ";
  }
  *out += label + ": ";
  for (std::size_t i = 0; i < values.size(); i++) {
    if (i > 0) {
      *out += ", ";
    }
    *out += values[i];
  }
}

}  // namespace

std::string describe_role_resolution(const RoleResolution &resolution) {
  if (resolution.ok()) {
    return std::string();
  }
  std::string out;
  append_clause(&out, "trigger profile does not bind these run roles", resolution.missing_roles);
  append_clause(&out, "roles bound more than once", resolution.duplicate_roles);
  // Worded as a camera-count mismatch: a binding for a role this run has no
  // camera for is a routing that does not match the topology.
  append_clause(&out, "routing does not match the camera count, no camera for these roles",
                resolution.unexpected_roles);
  append_clause(&out, "bindings name trigger channels the profile does not define", resolution.unknown_channels);
  return out;
}

}  // namespace v4l2diag
