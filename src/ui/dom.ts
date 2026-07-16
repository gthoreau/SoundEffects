/** DOM helpers. Deliberately tiny — the shell has no framework and wants none. */

type Attrs = Record<string, string | number | boolean | null | undefined>;

export function el<K extends keyof HTMLElementTagNameMap>(
  tag: K,
  attrs: Attrs = {},
  children: (Node | string)[] = [],
): HTMLElementTagNameMap[K] {
  const node = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === null || v === undefined || v === false) continue;
    if (k === "class") node.className = String(v);
    else if (k === "text") node.textContent = String(v);
    else node.setAttribute(k, v === true ? "" : String(v));
  }
  for (const c of children) node.append(c);
  return node;
}

/** Apply a QuickDraw rect as absolute pixels, relative to the parent's origin. */
export function placeRect(
  node: HTMLElement,
  rect: { top: number; left: number; bottom: number; right: number },
  originTop = 0,
  originLeft = 0,
): void {
  node.style.position = "absolute";
  node.style.left = `${rect.left - originLeft}px`;
  node.style.top = `${rect.top - originTop}px`;
  node.style.width = `${rect.right - rect.left}px`;
  node.style.height = `${rect.bottom - rect.top}px`;
}
