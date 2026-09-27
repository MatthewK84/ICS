import { expect, test } from "@playwright/test";

test("shows the heading and the canary station status", async ({ page }) => {
  await page.goto("/");
  await expect(page.getByRole("heading", { level: 1 })).toHaveText("ICS");
  await expect(page.getByText(/^Station 1: Connected \(\d+ s ago\)$/)).toBeVisible();
});
