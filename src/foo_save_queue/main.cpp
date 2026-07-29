#include "stdafx.h"

// ---------------------------------------------------------------------------
// Component version metadata.
//
// Exactly one DECLARE_COMPONENT_VERSION per DLL is required (since foobar2000
// 1.0): the troubleshooter and the component-update finder both rely on it.
// Keep it unique and bump it on every release.
// ---------------------------------------------------------------------------
DECLARE_COMPONENT_VERSION(
    "Save Queue",
    "0.1.0",
    "Save and restore the foobar2000 playback queue.\n\n"
    "Adds \"Save Queue\" commands to the File menu.");

// Locks the component to its canonical filename so users can't accidentally
// rename it (which would break the troubleshooter) or load two copies.
VALIDATE_COMPONENT_FILENAME("foo_save_queue.dll");

// Enables cfg_var downgrade handling when retargeting to an older SDK.
FOOBAR2000_IMPLEMENT_CFG_VAR_DOWNGRADE;
