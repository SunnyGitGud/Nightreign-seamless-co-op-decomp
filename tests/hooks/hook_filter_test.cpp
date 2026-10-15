#include "nrsc/hooks/hook_filter.hpp"

#include <gtest/gtest.h>

namespace nrsc::hooks {
namespace {

HookSpec named(std::string name) {
    HookSpec s;
    s.name = std::move(name);
    s.kind = Kind::BytePatch;
    s.site = 0;
    return s;
}

std::vector<std::string> names(const std::vector<HookSpec>& specs) {
    std::vector<std::string> out;
    for (const HookSpec& s : specs) out.push_back(s.name);
    return out;
}

TEST(DisabledHooksLine, TrimsOnlyTrailingNewlineCarriageReturnAndSpace) {
    EXPECT_EQ(parse_disabled_hooks_line("hook#20\r\n"), "hook#20");
    EXPECT_EQ(parse_disabled_hooks_line("P5  \n"), "P5");
    EXPECT_EQ(parse_disabled_hooks_line("\tP5\t\n"), "\tP5\t");  // tabs and leading blanks are kept, as before
    EXPECT_EQ(parse_disabled_hooks_line(" P5\n"), " P5");
}

TEST(DisabledHooksLine, SkipsBlankAndCommentLines) {
    EXPECT_EQ(parse_disabled_hooks_line("\n"), std::nullopt);
    EXPECT_EQ(parse_disabled_hooks_line("   \r\n"), std::nullopt);
    EXPECT_EQ(parse_disabled_hooks_line("# Diagnostic: hook-name prefixes to skip\n"), std::nullopt);
    EXPECT_EQ(parse_disabled_hooks_line("no-auto-host\n"), std::string(kNoAutoHostLine));
}

TEST(HeldBack, OnlyP12IsHeldBackByPrefix) {
    EXPECT_FALSE(held_back_reason("hook#7 scaling"));  // target ids recovered (devirt 2.11)
    EXPECT_FALSE(held_back_reason("hook#10 rows"));
    EXPECT_TRUE(held_back_reason("P12 bloodstain"));
    EXPECT_TRUE(held_back_reason("P120"));  // prefix match, as in the original filter
    EXPECT_FALSE(held_back_reason("hook#1 party"));
    EXPECT_FALSE(held_back_reason("hook#17"));
    EXPECT_FALSE(held_back_reason("P1 net"));
}

TEST(FilterHooks, RemovesDisabledThenHeldBackAndLogsInThatOrder) {
    std::vector<HookSpec> specs{named("P12 bloodstain"), named("hook#20 lobby"), named("hook#7 scaling"), named("P5 battle")};
    std::vector<std::string> log;
    filter_hooks(specs, {"hook#2", "", "P5"}, [&](const std::string& m) { log.push_back(m); });
    EXPECT_EQ(names(specs), std::vector<std::string>{"hook#7 scaling"});
    EXPECT_EQ(log, (std::vector<std::string>{
                       "disabled hook#20 lobby (nrsc_disable_hooks.txt)",
                       "disabled P5 battle (nrsc_disable_hooks.txt)",
                       "held back P12 bloodstain (crashes a new game with the 6-slot layout; see docs/plan_gaps.md)",
                   }));
}

TEST(FilterHooks, KeepsOrderOfRemainingSpecs) {
    std::vector<HookSpec> specs{named("P0"), named("P12"), named("hook#10"), named("P5"), named("hook#1")};
    filter_hooks(specs, {}, [](const std::string&) {});
    EXPECT_EQ(names(specs), (std::vector<std::string>{"P0", "hook#10", "P5", "hook#1"}));
}

}  // namespace
}  // namespace nrsc::hooks
