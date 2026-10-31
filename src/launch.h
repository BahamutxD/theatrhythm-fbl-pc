/* Double-click start (Windows): the command line run.py would pass, built from paths.ini. */
#ifndef THEATRHYTHM_LAUNCH_H
#define THEATRHYTHM_LAUNCH_H

/* theatrhythm-fbl.exe started with no boot file (no arguments, or only options): reads paths.ini
 * next to the exe (theatrhythm-setup.exe writes it), checks game\ was prepared for this release
 * (release.txt) and replaces *argc and *argv with
 * <exe> game\boot-linked.bin --content-profile game\content.bin --app0 <game> --user <save>
 * --timeout 0 [--isa-rewrite game\isa-rewrite.bin] and then the options given. Started from Explorer
 * (no console, no redirected output), output goes to <save>\log\theatrhythm.log; --console opens a
 * console window for it instead. 0 on success; otherwise it has told the user why. */
int launch_from_paths_ini(int *argc, char ***argv);

#endif
