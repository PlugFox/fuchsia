// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package fint

import (
	"context"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strings"
	"testing"

	"github.com/google/go-cmp/cmp"
	"github.com/google/go-cmp/cmp/cmpopts"
	"github.com/kr/pretty"
	"google.golang.org/protobuf/proto"
	"google.golang.org/protobuf/testing/protocmp"
	"google.golang.org/protobuf/types/known/structpb"

	"go.fuchsia.dev/fuchsia/tools/build"
	fintpb "go.fuchsia.dev/fuchsia/tools/integration/fint/proto"
)

type fakeBuildModules struct {
	archives         []build.Archive
	clippyTargets    []build.ClippyTarget
	generatedSources []string
	images           []build.Image
	pbinSets         []build.PrebuiltBinarySet
	testSpecs        []build.TestSpec
	tools            build.Tools
	zbiTests         []build.ZBITest
}

func (m fakeBuildModules) Archives() []build.Archive                     { return m.archives }
func (m fakeBuildModules) ClippyTargets() []build.ClippyTarget           { return m.clippyTargets }
func (m fakeBuildModules) GeneratedSources() []string                    { return m.generatedSources }
func (m fakeBuildModules) Images() []build.Image                         { return m.images }
func (m fakeBuildModules) PrebuiltBinarySets() []build.PrebuiltBinarySet { return m.pbinSets }
func (m fakeBuildModules) TestSpecs() []build.TestSpec                   { return m.testSpecs }
func (m fakeBuildModules) Tools() build.Tools                            { return m.tools }
func (m fakeBuildModules) ZBITests() []build.ZBITest                     { return m.zbiTests }

func TestBuild(t *testing.T) {
	platform := "linux-x64"
	artifactDir := filepath.Join(t.TempDir(), "artifacts")
	resetArtifactDir := func(t *testing.T) {
		// `artifactDir` is in the top-level tempdir so it can be referenced
		// in the `testCases` table, but that means it doesn't get cleared
		// between sub-tests so we need to clear it explicitly.
		if err := os.RemoveAll(artifactDir); err != nil {
			t.Fatal(err)
		}
		if err := os.MkdirAll(artifactDir, 0o700); err != nil {
			t.Fatal(err)
		}
	}

	testCases := []struct {
		name        string
		staticSpec  *fintpb.Static
		contextSpec *fintpb.Context
		// We want to skip the ninja no-op check in most tests because it
		// requires complicated mocking, but without setting
		// `SkipNinjaNoopCheck` on every test's context spec. This effectively
		// makes `SkipNinjaNoopCheck` default to true.
		ninjaNoopCheck bool
		modules        fakeBuildModules
		// Callback that is called by the fake runner whenever it starts
		// "running" a command, allowing each test to fake the result and output
		// of any subprocess.
		runnerFunc func(cmd []string, stdout io.Writer) error
		// List of regex strings, where each string corresponds to a subprocess
		// that must have been run by the runner.
		mustRun           []string
		expectedArtifacts *fintpb.BuildArtifacts
		expectErr         bool
	}{
		{
			name:              "empty spec produces no ninja targets",
			staticSpec:        &fintpb.Static{},
			expectedArtifacts: &fintpb.BuildArtifacts{},
			mustRun:           []string{`ninja -C .*out/default$`},
		},
		{
			name:       "artifact dir set",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				NinjaCompdbPath: filepath.Join(artifactDir, "compile-commands.json"),
				NinjaGraphPath:  filepath.Join(artifactDir, "ninja-graph.dot"),
			},
			mustRun: []string{`ninja .*-t graph`, `ninja .*-t compdb`},
		},
		{
			name:       "affected tests",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
				ChangedFiles: []*fintpb.Context_ChangedFile{
					{Path: "src/foo.py"},
				},
			},
			modules: fakeBuildModules{
				testSpecs: []build.TestSpec{
					{Test: build.Test{Name: "foo"}},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				NinjaCompdbPath: filepath.Join(artifactDir, "compile-commands.json"),
				NinjaGraphPath:  filepath.Join(artifactDir, "ninja-graph.dot"),
				LogFiles: map[string]string{
					"ninja dry run output": filepath.Join(artifactDir, "ninja_dry_run_output"),
				},
			},
		},
		{
			name: "incremental build",
			staticSpec: &fintpb.Static{
				Incremental: true,
			},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				NinjaCompdbPath: filepath.Join(artifactDir, "compile-commands.json"),
				NinjaGraphPath:  filepath.Join(artifactDir, "ninja-graph.dot"),
				LogFiles: map[string]string{
					"explain_output.txt": filepath.Join(artifactDir, "explain_output.txt"),
				},
			},
		},
		{
			name:           "failed ninja no-op check",
			staticSpec:     &fintpb.Static{},
			ninjaNoopCheck: true,
			expectErr:      true,
			expectedArtifacts: &fintpb.BuildArtifacts{
				FailureSummary: ninjaNoopFailureMessage(platform),
			},
		},
		{
			name:           "passed ninja no-op check",
			staticSpec:     &fintpb.Static{},
			ninjaNoopCheck: true,
			runnerFunc: func(cmd []string, stdout io.Writer) error {
				if contains(cmd, "-n") { // -n indicates ninja dry run.
					stdout.Write([]byte(noWorkString))
				}
				return nil
			},
			expectedArtifacts: &fintpb.BuildArtifacts{},
		},
		{
			name:       "ninja graph fails",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			runnerFunc: func(cmd []string, stdout io.Writer) error {
				if contains(cmd, "graph") {
					return fmt.Errorf("failed to run command: %s", cmd)
				}
				return nil
			},
			expectErr:         true,
			expectedArtifacts: &fintpb.BuildArtifacts{},
		},
		{
			name:       "ninja compdb fails",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			runnerFunc: func(cmd []string, stdout io.Writer) error {
				if contains(cmd, "compdb") {
					return fmt.Errorf("failed to run command: %s", cmd)
				}
				return nil
			},
			expectErr: true,
			expectedArtifacts: &fintpb.BuildArtifacts{
				NinjaGraphPath: filepath.Join(artifactDir, "ninja-graph.dot"),
			},
		},
		{
			name:       "ninja graph and compdb fail after failed build",
			staticSpec: &fintpb.Static{},
			contextSpec: &fintpb.Context{
				ArtifactDir: artifactDir,
			},
			// This will cause the main ninja build to fail, along with `ninja
			// compdb` and `ninja graph`.
			runnerFunc: func(cmd []string, stdout io.Writer) error {
				if strings.HasSuffix(cmd[0], "ninja") {
					if !contains(cmd, "-t") {
						stdout.Write([]byte("[0/1] CXX c.o d.o\nFAILED: c.o d.o\nsomeoutput\n"))
					}
					return fmt.Errorf("failed to run command: %s", cmd)
				}
				return nil
			},
			expectErr: true,
			expectedArtifacts: &fintpb.BuildArtifacts{
				// Even if post-processing steps like `ninja graph` fail, the
				// failure summary should still attribute the failure to the
				// original ninja build error.
				FailureSummary: "[0/1] CXX c.o d.o\nFAILED: c.o d.o\nsomeoutput\n",
			},
			mustRun: []string{`ninja .*-t graph`, `ninja .*-t compdb`},
		},
		{
			name: "extra ad-hoc ninja targets",
			staticSpec: &fintpb.Static{
				NinjaTargets: []string{"bar", "foo"},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"bar", "foo"},
			},
			mustRun: []string{`ninja .* bar foo`},
		},
		{
			name: "duplicate targets",
			staticSpec: &fintpb.Static{
				NinjaTargets: []string{"foo", "foo"},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"foo"},
			},
		},
		{
			name: "images for testing included",
			staticSpec: &fintpb.Static{
				IncludeImages: true,
			},
			modules: fakeBuildModules{
				images: []build.Image{
					{Name: qemuImageNames[0], Path: "qemu_image_path"},
					{Name: "should-be-ignored", Path: "different_path"},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltImages: []*structpb.Struct{
					mustStructPB(t, build.Image{Name: qemuImageNames[0], Path: "qemu_image_path"}),
				},
				BuiltTargets: append(extraTargetsForImages, "build/images:updates", "qemu_image_path"),
			},
		},
		{
			name: "images and archives included",
			staticSpec: &fintpb.Static{
				IncludeImages:   true,
				IncludeArchives: true,
			},
			modules: fakeBuildModules{
				archives: []build.Archive{
					{Name: "packages", Path: "p.tar.gz", Type: "tgz"},
					{Name: "archive", Path: "b.tar", Type: "tar"},
					{Name: "archive", Path: "b.tgz", Type: "tgz"},
					{Name: "other", Path: "other.tgz", Type: "tgz"},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltArchives: []*structpb.Struct{
					mustStructPB(t, build.Archive{Name: "packages", Path: "p.tar.gz", Type: "tgz"}),
					mustStructPB(t, build.Archive{Name: "archive", Path: "b.tgz", Type: "tgz"}),
				},
				BuiltTargets: append(extraTargetsForImages, "p.tar.gz", "b.tgz"),
			},
		},
		{
			name: "netboot images and scripts excluded when paving",
			staticSpec: &fintpb.Static{
				Pave:            true,
				IncludeImages:   true,
				IncludeArchives: true,
			},
			modules: fakeBuildModules{
				images: []build.Image{
					{Name: "netboot", Path: "netboot.zbi", Type: "zbi"},
					{Name: "netboot-script", Path: "netboot.sh", Type: "script"},
					{Name: "foo", Path: "foo.sh", Type: "script"},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltImages: []*structpb.Struct{
					mustStructPB(t, build.Image{Name: "foo", Path: "foo.sh", Type: "script"}),
				},
				BuiltTargets: append(extraTargetsForImages, "foo.sh"),
			},
		},
		{
			name: "default ninja target included",
			staticSpec: &fintpb.Static{
				IncludeDefaultNinjaTarget: true,
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{":default"},
			},
		},
		{
			name: "host tests included",
			staticSpec: &fintpb.Static{
				IncludeHostTests: true,
			},
			modules: fakeBuildModules{
				testSpecs: []build.TestSpec{
					{Test: build.Test{OS: "fuchsia", Path: "fuchsia_path"}},
					{Test: build.Test{OS: "linux", Path: "linux_path"}},
					{Test: build.Test{OS: "mac", Path: "mac_path"}},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"linux_path", "mac_path"},
			},
		},
		{
			name: "generated sources included",
			staticSpec: &fintpb.Static{
				IncludeGeneratedSources: true,
			},
			modules: fakeBuildModules{
				generatedSources: []string{"foo.h", "bar.h"},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"foo.h", "bar.h"},
			},
		},
		{
			name: "prebuilt binary manifests included",
			staticSpec: &fintpb.Static{
				IncludePrebuiltBinaryManifests: true,
			},
			modules: fakeBuildModules{
				pbinSets: []build.PrebuiltBinarySet{
					{Manifest: "manifest1.json"},
					{Manifest: "manifest2.json"},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"manifest1.json", "manifest2.json"},
			},
		},
		{
			name: "tools included",
			staticSpec: &fintpb.Static{
				Tools: []string{"tool1", "tool2"},
			},
			modules: fakeBuildModules{
				tools: makeTools(map[string][]string{
					"tool1": {"linux", "mac"},
					"tool2": {"linux"},
					"tool3": {"linux", "mac"},
				}),
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"linux_x64/tool1", "linux_x64/tool2"},
			},
		},
		{
			name: "all lint targets",
			staticSpec: &fintpb.Static{
				IncludeLintTargets: fintpb.Static_ALL_LINT_TARGETS,
			},
			modules: fakeBuildModules{
				clippyTargets: []build.ClippyTarget{
					{
						Output: "gen/src/foo.clippy",
						Sources: []string{
							"../../src/foo/x.rs",
							"../../src/foo/y.rs",
						},
					},
					{
						Output: "gen/src/bar.clippy",
						Sources: []string{
							"../../src/bar/x.rs",
							"../../src/bar/y.rs",
						},
					},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"gen/src/foo.clippy", "gen/src/bar.clippy"},
			},
		},
		{
			name: "affected lint targets",
			staticSpec: &fintpb.Static{
				IncludeLintTargets: fintpb.Static_AFFECTED_LINT_TARGETS,
			},
			contextSpec: &fintpb.Context{
				ChangedFiles: []*fintpb.Context_ChangedFile{
					{Path: "src/foo/x.rs"},
					{Path: "src/foo/y.rs"},
				},
			},
			modules: fakeBuildModules{
				clippyTargets: []build.ClippyTarget{
					{
						Output: "gen/src/foo.clippy",
						Sources: []string{
							"../../src/foo/x.rs",
							"../../src/foo/y.rs",
						},
					},
					{
						Output: "gen/src/bar.clippy",
						Sources: []string{
							"../../src/bar/x.rs",
							"../../src/bar/y.rs",
						},
					},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltTargets: []string{"gen/src/foo.clippy"},
			},
		},
		{
			name: "nonexistent tool",
			staticSpec: &fintpb.Static{
				Tools: []string{"tool1"},
			},
			expectErr: true,
		},
		{
			name: "tool not supported on current platform",
			staticSpec: &fintpb.Static{
				Tools: []string{"tool1"},
			},
			modules: fakeBuildModules{
				tools: makeTools(map[string][]string{
					"tool1": {"mac"},
				}),
			},
			expectErr: true,
		},
		{
			name: "zbi tests",
			staticSpec: &fintpb.Static{
				TargetArch:      fintpb.Static_ARM64,
				IncludeZbiTests: true,
			},
			modules: fakeBuildModules{
				zbiTests: []build.ZBITest{
					{
						Name:        "foo",
						Label:       "//src/foo",
						DeviceTypes: []string{"AEMU"},
						Path:        "foo.zbi",
					},
					{
						Name:        "bar",
						Label:       "//src/bar",
						DeviceTypes: []string{"Intel NUC Kit NUC7i5DNHE"},
						Path:        "bar.zbi",
					},
				},
				images: []build.Image{
					{
						Name:  qemuKernelImageName,
						Label: "//src/foo",
						Path:  "foo-qemu-kernel",
					},
					{
						Name: "fastboot",
						Path: "fastboot",
					},
					{
						Name:            "zircon-a",
						PaveZedbootArgs: []string{"--boot", "--zircona"},
						Path:            "zircona",
					},
					{
						Name:            "zircon-r",
						PaveZedbootArgs: []string{"--zirconr"},
						Path:            "zirconr",
					},
				},
			},
			expectedArtifacts: &fintpb.BuildArtifacts{
				BuiltZedbootImages: []*structpb.Struct{
					mustStructPB(t, build.Image{
						Name:            "zircon-a",
						PaveZedbootArgs: []string{"--boot", "--zircona", "--zirconb"},
						Path:            "zircona",
					}),
					mustStructPB(t, build.Image{
						Name:            "zircon-r",
						PaveZedbootArgs: []string{"--zirconr"},
						Path:            "zirconr",
					}),
				},
				BuiltTargets: []string{"foo.zbi", "bar.zbi", "foo-qemu-kernel", "zircona", "zirconr"},
				ZbiTestQemuKernelImages: map[string]*structpb.Struct{
					"foo": mustStructPB(t, build.Image{
						Name:  qemuKernelImageName,
						Label: "//src/foo",
						Path:  "foo-qemu-kernel",
						Type:  "kernel",
					}),
				},
			},
		},
	}
	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			resetArtifactDir(t)

			checkoutDir := t.TempDir()
			buildDir := filepath.Join(checkoutDir, "out", "default")

			defaultContextSpec := &fintpb.Context{
				SkipNinjaNoopCheck: !tc.ninjaNoopCheck,
				CheckoutDir:        checkoutDir,
				BuildDir:           buildDir,
			}
			proto.Merge(defaultContextSpec, tc.contextSpec)
			tc.contextSpec = defaultContextSpec
			runner := &fakeSubprocessRunner{run: tc.runnerFunc}
			tc.modules.tools = append(tc.modules.tools, makeTools(
				map[string][]string{
					"gn":    {"linux", "mac"},
					"ninja": {"linux", "mac"},
				},
			)...)
			ctx := context.Background()
			artifacts, err := buildImpl(
				ctx, runner, tc.staticSpec, tc.contextSpec, tc.modules, platform)
			if err != nil {
				if !tc.expectErr {
					t.Fatalf("Got unexpected error: %s", err)
				}
			} else if tc.expectErr {
				t.Fatal("Expected an error but got nil")
			}

			if tc.expectedArtifacts == nil {
				tc.expectedArtifacts = &fintpb.BuildArtifacts{}
			}
			if len(runner.commandsRun) > 0 {
				// If preprocessing fails before we run any subprocesses, then
				// NinjaLogPath will not be set.
				tc.expectedArtifacts.NinjaLogPath = filepath.Join(buildDir, ninjaLogPath)
			}
			opts := cmp.Options{
				protocmp.Transform(),
				// Ordering of the repeated artifact fields doesn't matter.
				cmpopts.SortSlices(func(a, b string) bool { return a < b }),
			}
			if diff := cmp.Diff(tc.expectedArtifacts, artifacts, opts...); diff != "" {
				t.Errorf("Got wrong artifacts (-want +got):\n%s", diff)
			}

			for _, s := range tc.mustRun {
				re, err := regexp.Compile(s)
				if err != nil {
					t.Fatal(err)
				}
				found := false
				for _, cmd := range runner.commandsRun {
					if re.MatchString(strings.Join(cmd, " ")) {
						found = true
						break
					}
				}
				if !found {
					t.Errorf("No command was run matching %q. Commands run: %s", s, pretty.Sprint(runner.commandsRun))
				}
			}
		})
	}
}

func makeTools(supportedOSes map[string][]string) build.Tools {
	var res build.Tools
	for toolName, systems := range supportedOSes {
		for _, os := range systems {
			res = append(res, build.Tool{
				Name: toolName,
				OS:   os,
				CPU:  "x64",
				Path: fmt.Sprintf("%s_x64/%s", os, toolName),
			})
		}
	}
	return res
}

// mustStructPB converts a Go struct to a protobuf Struct, failing the test in
// case of failure.
func mustStructPB(t *testing.T, s interface{}) *structpb.Struct {
	ret, err := toStructPB(s)
	if err != nil {
		t.Fatal(err)
	}
	return ret
}

func Test_gnCheckGenerated(t *testing.T) {
	ctx := context.Background()
	runner := fakeSubprocessRunner{
		mockStdout: []byte("check error\n"),
		fail:       true,
	}
	output, err := gnCheckGenerated(ctx, &runner, "gn", t.TempDir(), t.TempDir())
	if err == nil {
		t.Fatalf("Expected gn check to fail")
	}
	if diff := cmp.Diff(string(runner.mockStdout), output); diff != "" {
		t.Errorf("Got wrong gn check output (-want +got):\n%s", diff)
	}
}
