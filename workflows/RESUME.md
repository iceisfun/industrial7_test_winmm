# Resuming the winmm (industrial7_test_winmm) workflow(s)

Suspended on 2026-11-10 when the weekly Claude usage budget ran out. This branch (`workflow`) holds the repo **as-is at suspension** plus
the Workflow scripts that were driving it. master on GitHub is NOT updated by this branch.

Research (docs/research/01-04 plus area plan) and Foundation (VM audio) done; many area writers/reviewers had run on the Win10 VM with a virtual HDA device.

## What is in workflows/
- `winmm-corpus-wf_7c1352b1-45d.js`: the ultracode Workflow scripts (plain JavaScript for the Claude Code `Workflow` tool; read the header comment, phases are in `meta`).
- `journals/*.journal.jsonl`: one JSON line per completed agent (its full return value) for the run ids wf_7c1352b1-45d. Read these to see exactly what finished.

## State at suspension
- areas/test dirs under tests/: 19, of which 15 have an EVIDENCE.md (written by the Win10-validation step)
- git log on this branch shows every commit the agents made; uncommitted work was committed into the first commit on this branch (`as-is snapshot`).
- Not completed: areas without EVIDENCE.md, Surface/Coverage/Gapfill, the full Win10 pass, docs, push to master

## How to resume (another lane / another Claude Code session)
1. Clone, `git checkout workflow`. Paths inside the scripts are absolute for the original host (`/home/iceisfun/...`): edit the constants at the top (ROOT, TP = the test_pattern checkout https://github.com/iceisfun/test_pattern if available, LAB, I7 = the industrial7 checkout) and the model constants (OPUS / SON) to what you can afford.
2. The original run's agent cache is session-local, so `resumeFromRunId` will not work from another session. Instead run the script again with `Workflow({scriptPath})` after trimming it: delete the phases/agents whose output already exists in the repo (the journals and git log show which), e.g. keep only the Write+Review+Win10 pipeline over areas without a tests/<area>/EVIDENCE.md, then Gapfill, Final.
3. Win10 VM: the scripts use the local QEMU recipe (win10_run.sh around the `bb depot get m2` base image, payload ISO with run.cmd, results over COM1). Any lane with a Win10 22H2 machine or VM can replace tools/vm_run.sh.
4. Cheaper models: change the model only on agents that have not run yet (finished ones are not re-run anyway when you trim the script).
5. Final phase pushes to master (`git push -u origin master`): merge this branch into master first.

Posted by a non-MCP Claude Code instance under the lanea identity.
