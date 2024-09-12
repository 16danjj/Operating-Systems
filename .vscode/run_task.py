import os
import json
import sys

def main():
  workspace_folder = sys.argv[1]
  project_name = sys.argv[2]
  exe = sys.argv[3]
  gdb = sys.argv[4]

  build_dir = workspace_folder + "/src/" + project_name + "/build"
  sample_dir = workspace_folder + "/src/tests/" + project_name

  with open("tests_aux.json") as f_json:
    aux = json.load(f_json)

    # Go to build directory
    os.chdir(build_dir)

    # Always add the executable to the file system
    exe_basename = os.path.basename(exe)
    cmd = "pintos -p {} -a {} -- -q".format(exe, exe_basename)
    os.system(cmd)

    args = []
    pnames = []

    if exe in aux:
      args = aux[exe]["args"]
      pnames = aux[exe]["put"]

    # Put extra files in file system for tests that need them
    for pname in pnames:
      put_basename = os.path.basename(pname)

      # Hacky because sample.txt is not in the build directory
      if "sample.txt" == put_basename:
        pname = sample_dir + "/sample.txt"
      else:
        pname = build_dir + "/" + pname

      cmd = "pintos -p {} -a {} -- -q".format(pname, put_basename)
      os.system(cmd)

    # Pass debugger flag so that we can use the vscode debugger
    cmd = "pintos -v -k -T 60 --swap-size=4 "
    if gdb == "1":
      cmd = cmd + "--gdb"

    # Pass arguments to tests that need them
    if len(args) > 0:
      cmd = cmd + " -- -q  run '{} {}'".format(exe_basename, args[0])
    else:
      cmd = cmd + " -- -q  run '{}'".format(exe_basename)

    os.system(cmd)


if __name__ == "__main__":
  main()