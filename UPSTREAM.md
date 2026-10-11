# Upstream provenance

`rockbox/` is based on [Rockbox](https://github.com/Rockbox/rockbox) commit
[`59f15ca63079f0bd735d0b02d686257bd7b5205b`](https://github.com/Rockbox/rockbox/commit/59f15ca63079f0bd735d0b02d686257bd7b5205b),
with this project's Shanling M3X changes. The development workspace retains the
upstream Git checkout; distributed source ZIPs omit Git metadata. `publish/` is
a local export, not the canonical place to edit source.

Existing file headers, copyright notices and component licenses are preserved.
See `rockbox/docs/COPYING`, `rockbox/docs/LICENSES`,
`rockbox/docs/COPYING-MANUAL` and individual component notices. The root `LICENSE`
reproduces Rockbox's GNU GPL version 2 text. The test MP3 has its own Creative
Commons Attribution 3.0 license and credits in `media/Sneaky_Snitch_credits.txt`.

Proprietary firmware/boot images, device dumps, recovery backups, downloaded
SDKs, signing keys and private development evidence are not part of the public
source archive. Local research and recovery files live under `local/`.
