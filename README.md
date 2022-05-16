![Debugger](https://repository-images.githubusercontent.com/492556883/82e69014-add9-44e8-aaed-44ecc3d8af6f)

# Squirrel ImGui Debugger

A simple interface for debugging Squirrel programming language scripts without the need for an additional IDE such as Visual Studio, VS Code, or Eclipse.

## Requirements

To use this library, your program must already support both [Squirrel](http://squirrel-lang.org/) and [Dear Imgui](https://github.com/ocornut/imgui).

For remote debugging, your program must additionally include support for [NetImgui](https://github.com/sammyfreg/netImgui) or a similar application.

## Release Notes
### v0.1
Initial release

## Getting started
### Build the library or embed the source
There are two methods for using the debugger:

1. Open the provided solution and build the library for either release or debug. Link to the generated .lib file in your program.
2. Copy the source code in the `src` folder to your project and include the source.

### Include headers
There are two major components to the debugger that you'll need to access: the VM manager and the debugger interface. Simply include these headers:

```
#include <d_interface.h>
#include <d_vm.h>
```

### Link to the library
squirrel_imgui_debugger.lib

### Attach your VM(s)
Attaching a squirrel VM is as simple as adding the following line of code anywhere in your program:

`rumDebugVM::AttachVM( mySquirrelVM );`

However, you'll probably want to design your program to attach programmatically based on a menu selection, keypress, or console command.

### Detach your VM(s)
Similarly, you can detach a VM with this line of code:

`rumDebugVM::DetachVM( mySquirrelVM );`

### Create an interface thread
You'll need to create a thread for the ImGui interface to run on separately from your main program thread. Here's some example code to get you started:

```
#define MY_NETIMGUI_PORT 8888U

std::thread myDebuggerThread;
bool shutdownRequested{ false };

void ScriptDebuggerThread( const std::string& strPath, const bool& shutdownRequested );

void ConnectScriptDebugger( const bool& shutdownRequested )
{
  myDebuggerThread = std::thread( &ScriptDebuggerThread,
                                  "path/to/squirrel/scripts",
                                  std::ref( shutdownRequested ) );
}

void ScriptDebuggerThread( const std::string& strPath, const bool& shutdownRequested )
{
  rumDebugInterface::Init( "myProgramName", MY_NETIMGUI_PORT, strPath );

  while( !shutdownRequested )
  {
    rumDebugInterface::Update();
  }

  rumDebugInterface::Shutdown();
}
```

Somewhere in your code, possibly during initialization, you'll need to call the `ConnectScriptDebugger` function so that the debugger thread is created. Again, however, you may want to consider designing your program so that the debugger thread is only executing when debugging is enabled.

### Shutdown
Don't forget to join your debugger thread:

```
if( myDebuggerThread.joinable() )
{
  myDebuggerThread.join();
}
```

### Configure settings
You can modify `d_settings.h` to change a few simple things such as the display resolution, how many lines variable previews will show, and the maximum length of filenames.

## Using the debugger
Once your VM is attached, ImGui should render a debugging environment similar to Visual Studio.

Available files are listed in a panel at the top left. Folders can be expanded and collapsed and a filter input is provided for quickly finding a file by name. A single click on any file will open the file in the code panel to the right.

In the code panel, you can press Ctrl+G to designate a line number to view. You can set a breakpoint in the line column by double-clicking. To toggle to a disabled breakpoint, double-click the breakpoint to change its color to yellow. You can remove a breakpoint by pressing 'delete' on the keyboard. You can also set a breakpoint from the code window by pressing F9 on the matching line.

When program execution pauses at a breakpoint, you can hover over source to get preview information for various symbols. You can also right-click on a symbol to copy the symbol name or add the symbol to the Watched section in the bottom left panel.

Watched variable names can be modified at any time, or you can right-click on the variable to delete the entry. You can also manually add a watch variable at any time by entering its name in the + input box at the bottom of the list.

Next to the Watch tab is a tab called Locals. When the program is paused at a breakpoint, all available local variables are populated here at the current stack level.

In the final panel at the bottom right, you can find both the Callstack and Breakpoints tabs.

The Callstack tab is only populated when the program is paused at a breakpoint. You can switch the stack level by double-clicking on the stack row. This will automatically bring the file and line into focus and all local variable info at the request stack level will be reflected in the Locals tab on the left.

The Breakpoints tab shows all currently set breakpoints and whether or not they're enabled or disabled. You can toggle breakpoints by double-clicking on the first column. Otherwise, you can jump directly to a breakpoint by double-clicking on its row.

While paused at a breakpoint, you can:
1. Resume execution by pressing F5
2. Step into a function by pressing F10
3. Step over a function by pressing F11
4. Step out of a function by pressing Shift+F11

## Debugger persistence
While using the debugger, breakpoint changes, opened files, and watched variables are all saved to the imgui.ini in the `[UserData][Script Debugger]` section.

Example:
```
[UserData][Script Debugger]
Breakpoint1=1143,0,export/client/scripts/client/title.nut
Breakpoint2=1172,1,export/client/scripts/client/title.nut
File1=export/client/scripts/client/title.nut
File2=export/client/scripts/client/ultima.nut
WatchVariable1=testx
WatchVariable2=intTest
WatchVariable3=nullTest
WatchVariable4=localTableTest
```

You can manually edit that information if needed.
