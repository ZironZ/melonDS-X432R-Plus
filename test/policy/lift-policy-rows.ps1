# Copyright 2026 ZironZ
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Lifts a frame window from a whole-scene renderer-details CSV into a C++
# POLICY_TEST block for test/policy/RecordedCaseTests.cpp.
#
# Reconstructs the WholeSceneCaptureProductUseInputs that the renderer built
# at the CanUseWholeSceneCaptureProduct call site (GPU2D_OpenGL_CaptureAdapter
# ResolveCaptureProduct) from the recorded columns, and asserts the decision
# the CSV recorded. Run against a CSV from a build whose behavior the user has
# visually verified - lifted rows are ground truth by verification, so record
# fresh CSVs via --renderer-test rather than digging up old ones (older CSVs
# may predate columns this script needs, and predate fixes).
#
# Usage:
#   .\lift-policy-rows.ps1 -Csv <renderer-details.csv> -From 8634 -To 8656 -Name HotelDuskFadeOut
#   .\lift-policy-rows.ps1 -Csv <csv> -From 100 -To 400 -Name DQVLogo -Engine A -OutFile block.cpp
#
# Column -> input mapping (per engine prefix a_/b_):
#   PolicyAccepted          <- capture_repr_effect_action != 5 (Fallback = policy rejected)
#   HasTexture              <- source_a_chosen_product_tex != 0
#   RequestKind             <- capture_policy_request_kind
#   ProductKind             <- source_a_chosen_product_kind
#   ProofKind               <- capture_policy_proof_kind
#   RenderAction            <- source_a_chosen_product_render_action
#   PresentationClass       <- source_a_chosen_product_class
#   ProductPresentationHash <- source_a_request_capture_presentation_hash
#   RequestPresentationHash <- source_a_request_current_presentation_hash
#   HasStoredEffectState    <- capture_repr_stored_effect_state != 0
#       (a stored-but-inactive state also reads 0; that ambiguity is
#        decision-equivalent, the guard cannot fire either way)
#   Stored/ConsumeEffectActive <- packed (mode<<8)|factor from
#       capture_repr_stored_effect_state / capture_repr_consume_effect_state
# Expected outputs:
#   Accepted                 <- capture_repr_product_use_accepted
#   EffectPhaseIncompatible  <- capture_repr_effect_phase_incompatible
#   RequiresRePresentation   <- only when capture_repr_effect_action makes it
#       unambiguous (4=NeedsRePresentation -> true; 1/2 -> false; 3 masked)
#
# Known trace limitation: effect-application columns can under-report effects
# applied after the capture-policy decision. For example, an EVY brightness
# effect applied by the final blit is not represented by
# capture_repr_effect_action. This script asserts only decision fields, which
# are recorded directly from the policy result, so the limitation does not
# affect lifted rows.
param(
    [Parameter(Mandatory = $true)] [string]$Csv,
    [Parameter(Mandatory = $true)] [int]$From,
    [Parameter(Mandatory = $true)] [int]$To,
    [Parameter(Mandatory = $true)] [string]$Name,
    [ValidateSet('A', 'B', 'Both')] [string]$Engine = 'Both',
    [switch]$IncludeFallback,
    [switch]$NoDedupe,
    [string]$OutFile
)

if ($Name -notmatch '^[A-Za-z_][A-Za-z0-9_]*$') {
    throw "-Name must be a valid C++ identifier (got '$Name')."
}

$requestKindNames = @('None', 'LiveOverlayProducer', 'CapturedLayerConsumer', 'HandoffConsumer', 'DirectFinalConsumer', 'MainVRAMDisplayConsumer')
$productKindNames = @('None', 'RouteProduct', 'RouteEventProduct', 'RouteStateProduct', 'BackgroundProduct', 'FullCaptureProduct', 'HandoffSnapshot', 'ParentOutput3D')
$proofKindNames = @('None', 'ExactCaptureEvent', 'RouteStateIdentity', 'ActiveBackgroundEpoch', 'HandoffRouteKey', 'DirectFinalPresentationMatch', 'CurrentOverlayEligibility', 'Source3DSceneIdentity')
$renderActionNames = @('None', 'BlitExactProduct', 'CompositeCurrentOverlay', 'RenderHandoffHybrid', 'RenderNormalHybridFallback')
$presentationClassNames = @('None', 'RawContent', 'AlreadyPresented', 'Fallback', 'Unknown')
$effectActionNames = @('None', 'DisplayAsIs', 'ApplyOnBlit', 'CompositeCurrentOverlay', 'NeedsRePresentation', 'Fallback', 'Reject')

function EnumLiteral([string]$type, [long]$value, [string[]]$names) {
    if ($value -ge 0 -and $value -lt $names.Count) { return "${type}::$($names[$value])" }
    return "static_cast<${type}>($value)"
}

function PackedEffectActive([long]$packed) {
    $mode = ($packed -shr 8) -band 0x3
    $factor = $packed -band 0xFF
    return ($mode -eq 1 -or $mode -eq 2) -and $factor -gt 0
}

$neededColumns = @(
    'capture_policy_request_kind', 'capture_policy_proof_kind',
    'capture_repr_effect_action', 'capture_repr_product_use_accepted',
    'capture_repr_effect_phase_incompatible',
    'capture_repr_stored_effect_state', 'capture_repr_consume_effect_state',
    'source_a_chosen_product_tex', 'source_a_chosen_product_kind',
    'source_a_chosen_product_render_action', 'source_a_chosen_product_class',
    'source_a_request_capture_presentation_hash',
    'source_a_request_current_presentation_hash',
    'path')

$engines = switch ($Engine) { 'A' { @('a') } 'B' { @('b') } default { @('a', 'b') } }

$reader = [System.IO.StreamReader]::new((Resolve-Path $Csv).Path)
try {
    $header = $reader.ReadLine().Split(',')
    $indexOf = @{}
    for ($i = 0; $i -lt $header.Count; $i++) { $indexOf[$header[$i]] = $i }

    foreach ($prefix in $engines) {
        foreach ($col in $neededColumns) {
            if (-not $indexOf.ContainsKey("${prefix}_${col}")) {
                throw "CSV is missing column ${prefix}_${col} - recorded by an older build? Re-record with the current build's renderer-test harness."
            }
        }
    }
    $frameIdx = $indexOf['frame']

    $vectors = New-Object System.Collections.Generic.List[object]
    while ($null -ne ($line = $reader.ReadLine())) {
        $cells = $line.Split(',')
        $f = 0
        if (-not [int]::TryParse($cells[$frameIdx], [ref]$f)) { continue }
        if ($f -lt $From) { continue }
        if ($f -gt $To) { break }

        foreach ($prefix in $engines) {
            $get = { param($col) [long]$cells[$indexOf["${prefix}_${col}"]] }

            $effectAction = & $get 'capture_repr_effect_action'
            if ($effectAction -eq 0) { continue }
            if ($effectAction -eq 5 -and -not $IncludeFallback) { continue }

            $stored = & $get 'capture_repr_stored_effect_state'
            $consume = & $get 'capture_repr_consume_effect_state'

            $vectors.Add([pscustomobject]@{
                Frame = $f
                EnginePrefix = $prefix
                Path = & $get 'path'
                EffectAction = $effectAction
                PolicyAccepted = $effectAction -ne 5
                HasTexture = (& $get 'source_a_chosen_product_tex') -ne 0
                RequestKind = & $get 'capture_policy_request_kind'
                ProductKind = & $get 'source_a_chosen_product_kind'
                ProofKind = & $get 'capture_policy_proof_kind'
                RenderAction = & $get 'source_a_chosen_product_render_action'
                PresentationClass = & $get 'source_a_chosen_product_class'
                ProductHash = & $get 'source_a_request_capture_presentation_hash'
                RequestHash = & $get 'source_a_request_current_presentation_hash'
                HasStoredEffectState = $stored -ne 0
                StoredEffectActive = PackedEffectActive $stored
                ConsumeEffectActive = PackedEffectActive $consume
                ExpectedAccepted = (& $get 'capture_repr_product_use_accepted') -ne 0
                ExpectedPhaseIncompatible = (& $get 'capture_repr_effect_phase_incompatible') -ne 0
            })
        }
    }
}
finally { $reader.Close() }

if ($vectors.Count -eq 0) {
    Write-Warning "No usable decision rows in frame range $From-$To (effect_action was 0/Fallback everywhere)."
    return
}

# Collapse identical decision vectors anywhere in the window (per engine).
# The policy function is pure, so row order is irrelevant to the test; games
# like Hotel Dusk alternate two vectors on the per-frame A/B cadence, which a
# consecutive-run dedupe would never collapse. Emitted in first-seen order;
# the comment keeps the contributing frames.
$inputKeys = 'EnginePrefix', 'Path', 'EffectAction', 'PolicyAccepted', 'HasTexture', 'RequestKind',
             'ProductKind', 'ProofKind', 'RenderAction', 'PresentationClass', 'ProductHash',
             'RequestHash', 'HasStoredEffectState', 'StoredEffectActive', 'ConsumeEffectActive',
             'ExpectedAccepted', 'ExpectedPhaseIncompatible'
$groups = New-Object System.Collections.Generic.List[object]
$groupByKey = @{}
foreach ($v in $vectors) {
    $key = ($inputKeys | ForEach-Object { $v.$_ }) -join '|'
    if (-not $NoDedupe -and $groupByKey.ContainsKey($key)) {
        $g = $groupByKey[$key]
        $g.Count++
        if ($g.Frames.Count -lt 4) { [void]$g.Frames.Add($v.Frame) }
        $g.LastFrame = $v.Frame
    }
    else {
        $frames = New-Object System.Collections.Generic.List[int]
        [void]$frames.Add($v.Frame)
        $g = [pscustomobject]@{ Key = $key; Vector = $v; Frames = $frames; LastFrame = $v.Frame; Count = 1 }
        $groups.Add($g)
        if (-not $NoDedupe) { $groupByKey[$key] = $g }
    }
}

$csvName = Split-Path -Leaf $Csv
$bool = @{ $true = 'true'; $false = 'false' }

$sb = [System.Text.StringBuilder]::new()
[void]$sb.AppendLine("// Lifted from $csvName")
[void]$sb.AppendLine("// frames $From-$To, engine(s): $($engines -join ',') - generated by lift-policy-rows.ps1, do not hand-edit values.")
[void]$sb.AppendLine("POLICY_TEST(Recorded_$Name)")
[void]$sb.AppendLine('{')
foreach ($g in $groups) {
    $v = $g.Vector
    $frameLabel = if ($g.Count -eq 1) { "frame $($g.Frames[0])" }
                  else { "frames $($g.Frames -join ',')..$($g.LastFrame) ($($g.Count) rows)" }
    $actionName = if ($v.EffectAction -lt $effectActionNames.Count) { $effectActionNames[$v.EffectAction] } else { $v.EffectAction }
    [void]$sb.AppendLine("    {")
    [void]$sb.AppendLine("        // $frameLabel, engine $($v.EnginePrefix.ToUpper()): path=$($v.Path), effect_action=$actionName")
    [void]$sb.AppendLine("        WholeSceneCaptureProductUseInputs inputs = {};")
    [void]$sb.AppendLine("        inputs.PolicyAccepted = $($bool[$v.PolicyAccepted]);")
    [void]$sb.AppendLine("        inputs.HasTexture = $($bool[$v.HasTexture]);")
    [void]$sb.AppendLine("        inputs.RequestKind = $(EnumLiteral 'WholeSceneCaptureRequestKind' $v.RequestKind $requestKindNames);")
    [void]$sb.AppendLine("        inputs.ProductKind = $(EnumLiteral 'WholeSceneCaptureProductKind' $v.ProductKind $productKindNames);")
    [void]$sb.AppendLine("        inputs.ProofKind = $(EnumLiteral 'WholeSceneCaptureProofKind' $v.ProofKind $proofKindNames);")
    [void]$sb.AppendLine("        inputs.RenderAction = $(EnumLiteral 'WholeSceneCaptureRenderAction' $v.RenderAction $renderActionNames);")
    [void]$sb.AppendLine("        inputs.PresentationClass = $(EnumLiteral 'WholeSceneCaptureProductPresentationClass' $v.PresentationClass $presentationClassNames);")
    [void]$sb.AppendLine(("        inputs.ProductPresentationHash = 0x{0:X}u;" -f $v.ProductHash))
    [void]$sb.AppendLine(("        inputs.RequestPresentationHash = 0x{0:X}u;" -f $v.RequestHash))
    [void]$sb.AppendLine("        inputs.HasStoredEffectState = $($bool[$v.HasStoredEffectState]);")
    [void]$sb.AppendLine("        inputs.StoredEffectActive = $($bool[$v.StoredEffectActive]);")
    [void]$sb.AppendLine("        inputs.ConsumeEffectActive = $($bool[$v.ConsumeEffectActive]);")
    [void]$sb.AppendLine("")
    [void]$sb.AppendLine("        const WholeSceneCaptureProductUseDecision decision =")
    [void]$sb.AppendLine("            CanUseWholeSceneCaptureProduct(inputs);")
    [void]$sb.AppendLine("        CHECK_EQ(decision.Accepted, $($bool[$v.ExpectedAccepted]));")
    [void]$sb.AppendLine("        CHECK_EQ(decision.EffectPhaseIncompatible, $($bool[$v.ExpectedPhaseIncompatible]));")
    # The adapter's effect-action ladder checks applyEffectOnBlit BEFORE
    # RequiresRePresentation, so ApplyOnBlit (2) and CompositeCurrentOverlay
    # (3) mask the re-presentation flag; only DisplayAsIs (1) proves it false
    # and NeedsRePresentation (4) proves it true.
    switch ($g.Vector.EffectAction) {
        1 { [void]$sb.AppendLine("        CHECK_EQ(decision.RequiresRePresentation, false);") }
        4 { [void]$sb.AppendLine("        CHECK_EQ(decision.RequiresRePresentation, true);") }
    }
    [void]$sb.AppendLine("    }")
}
[void]$sb.AppendLine('}')

$code = $sb.ToString()
if ($OutFile) {
    $outPath = if ([System.IO.Path]::IsPathRooted($OutFile)) { $OutFile }
               else { Join-Path (Get-Location).Path $OutFile }
    [System.IO.File]::WriteAllText($outPath, $code)
    Write-Host "Wrote $($groups.Count) row group(s) ($($vectors.Count) CSV rows) to $OutFile"
}
else {
    $code
    Write-Host "# $($groups.Count) row group(s) from $($vectors.Count) CSV rows. Paste into test/policy/RecordedCaseTests.cpp." -ForegroundColor DarkGray
}
