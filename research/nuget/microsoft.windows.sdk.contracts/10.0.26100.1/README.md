# Windows SDK contracts 10.0.26100.1 — XAML type and member names

The name sets a XAML harvester needs in order to classify markup, extracted
from the Windows SDK contract WinMD.

- Package: `Microsoft.Windows.SDK.Contracts`
- Version: `10.0.26100.1`
- WinMD: `ref/netstandard2.0/Windows.Foundation.UniversalApiContract.winmd`
  (6,094,336 bytes)
- WinMD SHA-256:
  `c3972b369892c4ce63f433057ebd26f87115c1d412d01561d12a171c5c987a39`
- Metadata version: `WindowsRuntime 1.4`
- Harvest: `xaml-members.json`

No package payload, WinMD, or other binary is committed. The WinMD SHA-256 above
is recorded so the harvest can be tied back to an exact input.

## Why this snapshot exists

Three questions about a piece of markup cannot be answered from the markup:

- is `<Foo>` a framework type or one of Terminal's own controls?
- is `Click="..."` a layout property or an event handler that only a code-behind
  can satisfy?
- can this element be the root of a standalone load, or is it a `Style` or a
  `ControlTemplate` that has no size of its own?

Answering them from a hand-written list would mean a list that rots silently.
Answering them from metadata means the answer is checkable and versioned.

## Contents

| set | size | what it answers |
| --- | ---: | --- |
| `class_local_names` | 1,751 | is this element name a framework type |
| `ui_element_local_names` | 177 | can this element be measured and arranged |
| `property_names` | 2,830 | is this attribute a property |
| `attached_property_names` | 1,181 | is this `Owner.Member` an attached property |
| `event_only_names` | 238 | is this attribute an event handler |
| `ambiguous_event_names` | 1 | names that are an event on one type and a property on another |

`ui_element_local_names` is computed by walking each class's `extends` chain to
`Windows.UI.Xaml.UIElement`, not by pattern-matching names.

Type names are taken from the whole `Windows.UI` namespace family, because
markup names types from outside `Windows.UI.Xaml`: `Color` is
`Windows.UI.Color` and `FontWeight` is `Windows.UI.Text.FontWeight`, and both
appear as resource values in Terminal's dictionaries. Property and event names
are taken from `Windows.UI.Xaml` alone, since widening them would import
unrelated members and weaken the event check rather than strengthen it.

An attached property is carried in metadata as the static DependencyProperty
`<Name>Property` plus a `Get`/`Set` pair, never as a plain property `<Name>`.
Markup spells it `Grid.Column`, so the harvest recovers the stem; without that
step every attached property in Terminal's markup reads as unknown.

`DownloadProgress` is the single name that is an event on one type and a
property on another. It is excluded from the event set, because rejecting markup
that merely sets the property would be the worse error.

## Validation

Applied to Terminal's 43 in-scope XAML files, these sets leave **no unknown
element type and no unknown attribute**. Every element name and every attribute
name in Terminal's WinUI markup is accounted for by the metadata, which is the
check that the scope above is drawn correctly.

## Reproduce

```bash
python3 -m pip install -r phase1/requirements-winui.txt
python3 phase1/scripts/download_nuget_package.py \
  --source https://api.nuget.org/v3/index.json \
  --package Microsoft.Windows.SDK.Contracts \
  --version 10.0.26100.1 \
  --output /tmp/sdk-contracts.nupkg
unzip -o /tmp/sdk-contracts.nupkg -d /tmp/sdk-contracts

python3 phase3/scripts/harvest_xaml_members.py \
  /tmp/sdk-contracts/ref/netstandard2.0/Windows.Foundation.UniversalApiContract.winmd \
  --output research/nuget/microsoft.windows.sdk.contracts/10.0.26100.1/xaml-members.json
```
