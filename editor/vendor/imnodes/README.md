# Vendored imnodes

MIT-licensed [Nelarius/imnodes](https://github.com/Nelarius/imnodes), copied from
`../promethium-fermide-systems-simulation` at project revision
`d294c65b80d97282662ce42e3602114df790ccf5` (the imnodes source was last changed there
at `007c583d415149b81074699d6d677b318659fe6b`). This is the same implementation
used by that application's Station Flow diagram and is compatible with our
vendored Dear ImGui 1.89.5. Builds do not depend on the sibling checkout.

Local patch: `ImNodesIO::LinkCreationEnabled` defaults to true. The Sector viewer
sets it false to prevent even transient link-drag previews on its read-only
ports. Link detachment is also suppressed on existing links/connected pins.
Node dragging, panning and selection are view-only; node positions are not
written to the Installation or its undo history.

The upstream MIT license is included in `LICENSE.md`.
