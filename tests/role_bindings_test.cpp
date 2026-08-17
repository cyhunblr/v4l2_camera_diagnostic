// Role-based trigger routing (plan item 2.5.1).
//
// Locks the canonical role set and the resolver contract. This is the seam both
// the CLI and the web server go through, so the rules live in exactly one place
// and neither caller can invent its own idea of a valid routing.
#include "v4l2diag/core/role_bindings.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool check(bool condition, const std::string &what) {
  if (!condition) {
    std::cerr << "FAIL: " << what << "\n";
  }
  return condition;
}

std::string join(const std::vector<std::string> &values) {
  std::string out;
  for (const auto &value : values) {
    if (!out.empty()) {
      out += ", ";
    }
    out += value;
  }
  return out;
}

bool has(const std::vector<std::string> &values, const std::string &needle) {
  return std::find(values.begin(), values.end(), needle) != values.end();
}

v4l2diag::RoleBinding role_to(const std::string &role, const std::string &channel) {
  v4l2diag::RoleBinding binding;
  binding.role = role;
  binding.trigger_channel_id = channel;
  return binding;
}

}  // namespace

int main() {
  using v4l2diag::RoleBinding;
  using v4l2diag::RoleResolution;
  bool ok = true;

  const std::vector<std::string> channels = {"gpio-0", "gpio-1"};

  // --- 1. The canonical role set comes from the run topology ---------------
  {
    ok &= check(v4l2diag::expected_roles(0) == std::vector<std::string>{"master"},
                "a run with no slaves expects roles: " + join(v4l2diag::expected_roles(0)));
    ok &= check(v4l2diag::expected_roles(2) == std::vector<std::string>({"master", "slave-1", "slave-2"}),
                "a run with two slaves expects roles: " + join(v4l2diag::expected_roles(2)));
    // 1-based numbering, fixed to the slaves[] order. A camera path may change;
    // the role name must not.
    ok &= check(v4l2diag::slave_role(0) == "slave-1", "slaves[0] is not slave-1");
    ok &= check(v4l2diag::slave_role(1) == "slave-2", "slaves[1] is not slave-2");
    ok &= check(std::string(v4l2diag::kMasterRole()) == "master", "the master role name changed");
  }

  // --- 2. An exact match resolves, in topology order ----------------------
  {
    // Deliberately out of order on input: the output must follow expected_roles().
    const RoleResolution res =
        v4l2diag::resolve_role_bindings({role_to("slave-1", "gpio-1"), role_to("master", "gpio-0")}, channels, 1);
    ok &= check(res.ok(), "an exact routing was rejected: " + v4l2diag::describe_role_resolution(res));
    ok &= check(res.resolved.size() == 2, "the resolution dropped a role");
    if (res.resolved.size() == 2) {
      ok &= check(res.resolved[0].role == "master" && res.resolved[0].trigger_channel_id == "gpio-0",
                  "master did not resolve first, or to the wrong channel");
      ok &= check(res.resolved[1].role == "slave-1" && res.resolved[1].trigger_channel_id == "gpio-1",
                  "slave-1 did not resolve second, or to the wrong channel");
    }
    ok &= check(v4l2diag::describe_role_resolution(res).empty(), "a successful resolution produced an error message");
  }

  // --- 3. A missing role is refused, never inferred -----------------------
  {
    // The case from the plan: a profile that only binds master, used for a run
    // that has a slave. Auto-completing this fires the wrong camera.
    const RoleResolution res = v4l2diag::resolve_role_bindings({role_to("master", "gpio-0")}, channels, 1);
    ok &= check(!res.ok(), "a run with an unbound slave-1 was accepted");
    ok &= check(has(res.missing_roles, "slave-1"), "slave-1 was not reported missing: " + join(res.missing_roles));
    ok &= check(res.resolved.empty(), "a failed resolution still handed back bindings to route with");
    ok &= check(v4l2diag::describe_role_resolution(res).find("slave-1") != std::string::npos,
                "the message does not name the missing role: " + v4l2diag::describe_role_resolution(res));
  }

  {
    // Not even a single-channel profile gets an automatic master. This is the
    // rule the CLI used to break by silently taking the only compatible channel.
    const RoleResolution res = v4l2diag::resolve_role_bindings({}, {"gpio-0"}, 0);
    ok &= check(!res.ok(), "a profile with exactly one channel got an automatic master binding");
    ok &= check(has(res.missing_roles, "master"), "master was not reported missing: " + join(res.missing_roles));
  }

  // --- 4. Duplicate roles are refused ------------------------------------
  {
    const RoleResolution res =
        v4l2diag::resolve_role_bindings({role_to("master", "gpio-0"), role_to("master", "gpio-1")}, channels, 0);
    ok &= check(!res.ok(), "a duplicate role was accepted");
    ok &=
        check(has(res.duplicate_roles, "master"), "the duplicate role was not reported: " + join(res.duplicate_roles));
  }

  // --- 5. Canonical surplus slave bindings are silently skipped -----------
  {
    // The profile binds slave-1 but this run has no slaves. A multi-camera
    // profile used with fewer cameras: the unused canonical binding is surplus,
    // not wrong. The run resolves with master only.
    const RoleResolution res =
        v4l2diag::resolve_role_bindings({role_to("master", "gpio-0"), role_to("slave-1", "gpio-1")}, channels, 0);
    ok &= check(res.ok(), "a surplus canonical slave binding rejected a single-camera run: " +
                              v4l2diag::describe_role_resolution(res));
    ok &= check(res.resolved.size() == 1, "the surplus binding appeared in the resolved output");
    ok &= check(res.unexpected_roles.empty(), "the surplus canonical slave was reported as unexpected");
  }

  {
    // Free text is not a role: a non-canonical name is always unexpected, even
    // when the run has no slave to conflict with it.
    const RoleResolution res =
        v4l2diag::resolve_role_bindings({role_to("master", "gpio-0"), role_to("primary", "gpio-1")}, channels, 0);
    ok &= check(!res.ok(), "a free-text role name was accepted");
    ok &= check(has(res.unexpected_roles, "primary"), "the free-text role was not reported as unexpected");
  }

  // --- 6. Unknown trigger channels are refused ---------------------------
  {
    const RoleResolution res = v4l2diag::resolve_role_bindings({role_to("master", "gpio-9")}, channels, 0);
    ok &= check(!res.ok(), "a binding naming a channel the profile does not define was accepted");
    ok &= check(has(res.unknown_channels, "gpio-9"),
                "the unknown channel was not reported: " + join(res.unknown_channels));
  }

  // --- 7. One channel may serve several roles ----------------------------
  {
    // Several cameras genuinely can sit on one physical GPIO line, so this is
    // explicitly allowed -- unlike a duplicate role.
    const RoleResolution res = v4l2diag::resolve_role_bindings(
        {role_to("master", "gpio-0"), role_to("slave-1", "gpio-0"), role_to("slave-2", "gpio-0")}, channels, 2);
    ok &= check(res.ok(), "one channel serving several roles was rejected: " + v4l2diag::describe_role_resolution(res));
    ok &= check(res.resolved.size() == 3, "the shared-channel routing lost a role");
    for (const auto &binding : res.resolved) {
      ok &= check(binding.trigger_channel_id == "gpio-0", "a shared-channel binding resolved elsewhere");
    }
  }

  // --- 8. Topology order, not lexicographic order ------------------------
  {
    // The order is only interesting past nine slaves, where the two disagree:
    // "slave-10" sorts before "slave-2". A std::map keyed by role name would look
    // correct for small runs and silently mis-order this one.
    std::vector<std::string> many_channels;
    std::vector<RoleBinding> many;
    many.push_back(role_to("master", "gpio-0"));
    many_channels.push_back("gpio-0");
    for (std::size_t i = 0; i < 11; i++) {
      const std::string channel = "gpio-" + std::to_string(i + 1);
      many_channels.push_back(channel);
      many.push_back(role_to(v4l2diag::slave_role(i), channel));
    }
    const RoleResolution res = v4l2diag::resolve_role_bindings(many, many_channels, 11);
    ok &= check(res.ok(), "an eleven-slave routing was rejected: " + v4l2diag::describe_role_resolution(res));
    if (check(res.resolved.size() == 12, "the eleven-slave routing lost a role")) {
      // Exactly expected_roles() order: master, slave-1, slave-2, ... slave-11.
      const std::vector<std::string> want = v4l2diag::expected_roles(11);
      std::vector<std::string> got;
      for (const auto &binding : res.resolved) {
        got.push_back(binding.role);
      }
      ok &= check(got == want, "resolved roles are not in topology order: " + join(got));
      // And each role kept its own channel, so the reorder did not shuffle values.
      for (std::size_t i = 0; i < res.resolved.size(); i++) {
        const std::string expected_channel = i == 0 ? "gpio-0" : "gpio-" + std::to_string(i);
        ok &= check(res.resolved[i].trigger_channel_id == expected_channel,
                    "role " + res.resolved[i].role + " resolved to " + res.resolved[i].trigger_channel_id +
                        " instead of " + expected_channel);
      }
    }
  }

  // --- 9. Every failure kind is reported at once -------------------------
  {
    // A caller fixing one problem at a time would otherwise need one round trip
    // per mistake. Uses a non-canonical role ("primary") to cover unexpected_roles,
    // since canonical surplus slave bindings are now silently skipped.
    const RoleResolution res = v4l2diag::resolve_role_bindings(
        {role_to("master", "nope"), role_to("master", "gpio-0"), role_to("primary", "gpio-1")}, channels, 1);
    ok &= check(!res.ok(), "a routing with four distinct faults was accepted");
    ok &= check(has(res.duplicate_roles, "master"), "the duplicate role went unreported");
    ok &= check(has(res.unknown_channels, "nope"), "the unknown channel went unreported");
    ok &= check(has(res.unexpected_roles, "primary"), "the non-canonical role went unreported");
    ok &= check(has(res.missing_roles, "slave-1"), "the missing role went unreported");
  }

  return ok ? 0 : 1;
}
