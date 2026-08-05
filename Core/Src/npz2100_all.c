/**
 * @file npz2100_all.c
 * @brief Aggregator — compiles all nPZ2100 driver sources in one translation unit.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * STM32CubeIDE managed projects add source files by folder (Source Location),
 * not individually. When using a CubeMX-generated project, the NPZ2100/Src/
 * folder is not automatically included in the build.
 *
 * Rather than requiring the client to modify project settings, this file
 * #include's the three driver sources directly. CubeIDE compiles this file
 * as part of Core/Src/ (which is always a source folder in any CubeMX project),
 * pulling in all three nPZ2100 translation units automatically.
 *
 * NO CHANGES to Project Properties are needed.
 *
 * If you prefer to add NPZ2100/Src/ as a proper source folder instead:
 *   Project → Properties → C/C++ Build → Settings →
 *   MCU GCC Compiler → Source Location → Add Folder → NPZ2100/Src
 * Then delete this file to avoid duplicate symbol errors.
 */

/* Include path for NPZ2100/Inc/ must be in the compiler include paths.
 * CubeMX-generated projects: add ../NPZ2100/Inc via
 *   Project → Properties → C/C++ Build → Settings →
 *   MCU GCC Compiler → Include paths → Add → ../NPZ2100/Inc          */

#include "../../NPZ2100/Src/npz2100.c"
#include "../../NPZ2100/Src/npz2100_mid.c"
#include "../../NPZ2100/Src/npz2100_stm32.c"
