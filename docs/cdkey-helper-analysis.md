# Static analysis of the disc CD-key helper

Historical initial inspection. The owner's later 2026-11-10 decision authorizes
independent validation reconstruction; [cdkey-check.md](cdkey-check.md) records
the implementation and address-level provenance that supersede the format-only
policy below.

Read-only inspection on 2026-11-10; **never executed**. No CD key was supplied to
these programs. Only PE metadata, imports, printable strings, UI resources and
the dialog's validation/result control flow were inspected. No validation
algorithm, constants, key derivation, test-key vectors or implementation is
reproduced here or added to setup.

The regular and Deluxe discs contain identical `Support/Battlefield 2_code.exe`
files: 323,584 bytes, SHA-256
`da59e9ac9e135282e80f19ba9ddc6d7b7535cc94b3c6dc19a94d34a9d2866d0e`.
This is a 32-bit Windows GUI PE, with an MFC dialog and statically linked runtime.
Version resources identify Electronic Arts' CDKey tool, internal/original name
CDKey.exe, version 1.04.05.00. The basename/product configuration and localized
resource messages associate the generic CDKey UI with Battlefield 2.

Deluxe also contains `Support_XP1/Battlefield 2 Special Forces_code.exe`:
323,584 bytes, SHA-256
`5671e943aa681b0c98d3121023b7a299d2b1bff69c9d325ce1fdc786352922c2`.
It is the corresponding SF product helper, with CDKey version 1.05.04.00 and
the same overall dialog/resource structure. It is also never executed by setup.

The resources include the code-entry dialog, five entry controls, instructions
to use the code printed with the disc/manual, Back/Next/Finish/Cancel controls,
and localized invalid-code and installation-stop messages. English dialog 102,
string-table blocks 7 and 9, and version resource 1 corroborate this role. Both
helpers contain 130 resource leaves across languages, including generic MFC
support resources; those generic messages are not evidence of product features.

Imports cover Windows UI, registry, file/runtime and shell facilities:
KERNEL32, USER32, GDI32, comdlg32, WINSPOOL, ADVAPI32, COMCTL32, SHLWAPI and
OLEAUT32. Registry read/create/set operations are available. The observed UI
combines the entered groups, calls an internal validation routine, branches to
local invalid-code messages on rejection, and hands successful input to its
local result/storage path. This is a local product-specific validity check,
beyond merely displaying a format-entry form. Its precise validation rules are
intentionally outside the analysis scope.

There are no socket/WinINet/WinHTTP imports or activation-server URL strings in
the inspected helper. The observed acceptance/rejection path is local; there is
no evidence here of an online activation/ownership check. This is a static
finding, not a claim about the game's separate multiplayer authentication or
all behavior of every EA registration component. Generic LoadLibrary/GetProcAddress
imports alone do not establish a network check.

Using this helper would not change data extraction, the patch chain or the
required SF source assets. It would introduce a Windows-executable dependency
and its own product-specific validity policy. The native installer therefore
continues the owner's requested format-only check, with separate BF2 and SF
rows for Deluxe, and writes its own private Registry.con. These helpers remain
inert support data when copied with the disc content. Nothing requires Wine,
execution of a CD-key helper or reproduction of a key algorithm.

Evidence retained under authorized scratch/setup: code-pe.txt, code-strings.txt,
code-ui-caller*.txt, *.resources.json and the regular/Deluxe inventory TSVs.
These local probe files are not distribution assets. Extracted EXEs and game
payloads are removed after the checks; reports contain no owner key.
