# daisy-kickstart

Quickly get up && running with Daisy 

## Getting Started

1. First, [install the Daisy Toolchain](https://daisy.audio/tutorials/cpp-dev-env/#1-install-the-toolchain). 
2. Once installed, run the `init.sh` script to configure your local copy of this repository. 
3. This repository is configured for building SRAM apps. Connect your Daisy Seed via USB and [install a bootloader](https://flash.daisy.audio/) before proceeding.
4. With your device in program mode, use `run.sh` (or `SHIFT+CMD+B` in VSCode) to build programs and flash them to your Daisy.

> [!NOTE]
> When developing for the Daisy, it is often useful to use serial monitoring for testing and debugging. Many examples in `examples/` demonstrate this. If developing in VSCode, we recommend installing Microsoft's [serial monitor extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode.vscode-serial-monitor), which will add easy access to serial monitoring via the terminal panel.