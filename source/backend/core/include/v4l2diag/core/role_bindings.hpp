#pragma once

#include <string>
#include <vector>

namespace v4l2diag {

// Role-based trigger routing (plan item 2.5).
//
// A Trigger Profile no longer identifies a physical camera. `/dev/videoN`, the
// sysfs path and the sysfs video index are not stable identities -- on this
// hardware a {driver, card, bus_info} matcher cannot even tell four nodes apart --
// so routing goes through run roles instead. The profile says "role master fires
// on channel gpio-0"; which device is master is decided by the run, not the
// profile.
//
// Roles are NOT free text. They are derived from the run topology, so the
// expected set is always knowable and a missing or extra binding is a hard error
// rather than something to guess at.

// The canonical master role, corresponding to RunConfig::master.
inline const char *kMasterRole() {
  return "master";
}

// The role for slaves[index], 0-based index -> "slave-1", "slave-2", ...
std::string slave_role(std::size_t index);

// One role -> one trigger channel. The same channel may serve several roles
// (several cameras genuinely can sit on one physical GPIO line); a role may not
// appear twice.
struct RoleBinding {
  std::string role;
  std::string trigger_channel_id;
};

// Whether `role` is a name the canonical set can ever produce: "master" or
// "slave-N" for N >= 1. Topology-independent, so a profile can be validated
// before any run exists; whether a role fits a PARTICULAR run is
// resolve_role_bindings()'s job.
bool is_canonical_role(const std::string &role);

// The roles a run expects, in topology order: master, slave-1, ... slave-N.
// `slave_count` is RunConfig::slaves.size().
std::vector<std::string> expected_roles(std::size_t slave_count);

// Outcome of matching a profile's bindings against a run's expected roles.
struct RoleResolution {
  // role -> trigger_channel_id, in expected_roles() order. Only filled when ok().
  std::vector<RoleBinding> resolved;

  // Roles the run needs that the profile does not bind. The user has to supply
  // these; they are never inferred, not even when the profile has exactly one
  // channel. Getting this wrong fires the wrong camera, which is worse than
  // making someone fill in a field.
  std::vector<std::string> missing_roles;
  // Roles bound more than once.
  std::vector<std::string> duplicate_roles;
  // Roles the profile binds that this run has no camera for. Reported as routing
  // that does not match the camera count.
  std::vector<std::string> unexpected_roles;
  // Bindings naming a channel the profile does not define.
  std::vector<std::string> unknown_channels;

  bool ok() const {
    return missing_roles.empty() && duplicate_roles.empty() && unexpected_roles.empty() && unknown_channels.empty();
  }
};

// Matches `bindings` against the roles a run with `slave_count` slaves expects.
// `channel_ids` are the trigger channel ids the profile defines.
//
// This is the single seam both the CLI and the web server use, so neither can
// drift into its own idea of what a valid routing is.
RoleResolution resolve_role_bindings(const std::vector<RoleBinding> &bindings,
                                     const std::vector<std::string> &channel_ids, std::size_t slave_count);

// One human-readable line for a failed resolution, empty when it succeeded.
// Shared so the CLI and the API refuse a run with the same words.
std::string describe_role_resolution(const RoleResolution &resolution);

}  // namespace v4l2diag
