import { describe, expect, it, vi } from "vitest";
import { render, screen } from "@testing-library/react";
import userEvent from "@testing-library/user-event";
import { SelectableCard } from "./SelectableCard";

// SelectableCard is the accessible-selection pattern the rest of the UI is
// meant to reuse, so its contract is locked here rather than re-derived per page.
describe("SelectableCard", () => {
  it("exposes selection state to assistive tech", () => {
    const { rerender } = render(<SelectableCard selected={false} onToggle={() => {}} title="MMAP" />);
    expect(screen.getByRole("button", { name: /MMAP/ })).toHaveAttribute("aria-pressed", "false");

    rerender(<SelectableCard selected onToggle={() => {}} title="MMAP" />);
    expect(screen.getByRole("button", { name: /MMAP/ })).toHaveAttribute("aria-pressed", "true");
  });

  it("toggles on click, Enter and Space", async () => {
    const user = userEvent.setup();
    const onToggle = vi.fn();
    render(<SelectableCard selected={false} onToggle={onToggle} title="T01" />);
    const card = screen.getByRole("button", { name: /T01/ });

    await user.click(card);
    expect(onToggle).toHaveBeenCalledTimes(1);

    card.focus();
    await user.keyboard("{Enter}");
    await user.keyboard(" ");
    expect(onToggle).toHaveBeenCalledTimes(3);
  });

  // Disabled cards stay reachable on purpose: the subtitle carries the reason
  // the card cannot be selected, and a keyboard user has to be able to hear it.
  it("marks a disabled card aria-disabled but keeps it focusable", async () => {
    const user = userEvent.setup();
    render(<SelectableCard selected={false} onToggle={() => {}} title="T01" subtitle="No capture support" disabled />);
    const card = screen.getByRole("button", { name: /T01/ });

    expect(card).toHaveAttribute("aria-disabled", "true");
    await user.tab();
    expect(card).toHaveFocus();
  });

  it("ignores click and keyboard while disabled", async () => {
    const user = userEvent.setup();
    const onToggle = vi.fn();
    render(<SelectableCard selected={false} onToggle={onToggle} title="T01" disabled />);
    const card = screen.getByRole("button", { name: /T01/ });

    await user.click(card);
    card.focus();
    await user.keyboard("{Enter}");
    await user.keyboard(" ");
    expect(onToggle).not.toHaveBeenCalled();
  });

  describe("corner action", () => {
    function renderWithCorner() {
      const onToggle = vi.fn();
      const onInfo = vi.fn();
      render(
        <SelectableCard
          selected={false}
          onToggle={onToggle}
          title="T01"
          cornerAction={<button type="button" onClick={onInfo}>info</button>}
        />
      );
      return { onToggle, onInfo };
    }

    // Nesting the trigger inside the selection button was both invalid markup
    // and a real bug: activating it bubbled up and toggled the card too.
    it("is not nested inside the selection button", () => {
      renderWithCorner();
      const select = screen.getByRole("button", { name: /T01/ });
      const info = screen.getByRole("button", { name: "info" });
      expect(select.contains(info)).toBe(false);
    });

    it("does not toggle the card when clicked", async () => {
      const user = userEvent.setup();
      const { onToggle, onInfo } = renderWithCorner();

      await user.click(screen.getByRole("button", { name: "info" }));
      expect(onInfo).toHaveBeenCalledTimes(1);
      expect(onToggle).not.toHaveBeenCalled();
    });

    it("does not toggle the card on Enter or Space", async () => {
      const user = userEvent.setup();
      const { onToggle, onInfo } = renderWithCorner();

      screen.getByRole("button", { name: "info" }).focus();
      await user.keyboard("{Enter}");
      await user.keyboard(" ");

      expect(onInfo).toHaveBeenCalledTimes(2);
      expect(onToggle).not.toHaveBeenCalled();
    });
  });
});
