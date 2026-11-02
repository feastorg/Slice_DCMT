"""Build the megaAVR core's Wire twi.c with its slave error branches fixed.

In framework-arduino-megaavr 1.8.8, TWI_SlaveInterruptHandler handles a
collision (COLL) or bus error (BUSERR) by calling only
TWI_SlaveTransactionFinished(), which writes nothing that clears the data or
address interrupt flag. The TWI slave interrupt then fires without end, holds
SCL low and starves every lower vector, millis() included
(feastorg/Slice_DCMT#101). Each branch now writes 1 to COLL and BUSERR and
TWI_SCMD_COMPTRANS_gc to SCTRLB first. Writing SCMD clears DIF and APIF and
releases SCL, so the slave waits for the next Start.

The core package is not modified. This script copies twi.c into the build
directory, applies the change there, and compiles the copy in place of the
original through a build middleware. It runs only for the atmelmegaavr
platform, and it stops the build if twi.c is not the 1.8.8 file it was
written against, so a core update cannot silently drop the fix.
"""

import hashlib
import os
import re

Import("env")  # noqa: F821 (SCons)

STOCK_SHA256 = "f5714f1dc2e2e6772b7972442c1551461a14696d4d95d8a2cb9324e0e041805f"
CLEAR = "TWI0.SSTATUS = TWI_COLL_bm | TWI_BUSERR_bm;"
COMPLETE = "TWI0.SCTRLB = TWI_SCMD_COMPTRANS_gc;"
RESULTS = ("TWIS_RESULT_BUS_ERROR", "TWIS_RESULT_TRANSMIT_COLLISION")

if env.PioPlatform().name == "atmelmegaavr":  # noqa: F821
    framework = env.PioPlatform().get_package_dir("framework-arduino-megaavr")  # noqa: F821
    stock = os.path.join(framework, "libraries", "Wire", "src", "utility", "twi.c")
    with open(stock, "rb") as f:
        raw = f.read()
    digest = hashlib.sha256(raw).hexdigest()
    if digest != STOCK_SHA256:
        raise SystemExit(
            f"patch_megaavr_twi.py: {stock} has sha256 {digest}, not the 1.8.8 file "
            f"{STOCK_SHA256}; check the core's slave error branches and update the patch"
        )

    # 1.8.8's twi.c has CRLF line endings; keep whichever the file uses.
    text = raw.decode("latin-1")
    for result in RESULTS:
        pattern = re.compile(
            r"^([ \t]*)(TWI_SlaveTransactionFinished\(" + result + r"\);)(\r?)$", re.M
        )
        matches = pattern.findall(text)
        if len(matches) != 1:
            raise SystemExit(
                f"patch_megaavr_twi.py: expected one call with {result} in twi.c, "
                f"found {len(matches)}"
            )
        text = pattern.sub(
            lambda m: (
                f"{m.group(1)}{CLEAR}{m.group(3)}\n"
                f"{m.group(1)}{COMPLETE}{m.group(3)}\n"
                f"{m.group(1)}{m.group(2)}{m.group(3)}"
            ),
            text,
        )

    patched_dir = os.path.join(env.subst("$BUILD_DIR"), "patched_twi")  # noqa: F821
    os.makedirs(patched_dir, exist_ok=True)
    patched = os.path.join(patched_dir, "twi.c")
    with open(patched, "w", encoding="latin-1") as f:
        f.write(text)

    stock_real = os.path.realpath(stock)

    def use_patched(node_env, node):
        if os.path.realpath(node.srcnode().get_abspath()) != stock_real:
            return node
        return node_env.Object(
            target=os.path.join(patched_dir, "twi.o"),
            source=patched,
            CPPPATH=node_env.get("CPPPATH", []) + [os.path.dirname(stock)],
        )

    env.AddBuildMiddleware(use_patched, "*twi.c")  # noqa: F821
