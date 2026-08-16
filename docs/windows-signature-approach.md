## Implementation Pathway Option

### 1. Add Windows version metadata

`CMakeLists.txt` declares version `0.1.4`, but the Windows resource file currently contains only the application icon:

```rc
IDI_ICON1 ICON "../img/logo.ico"
```

There is no Windows `VERSIONINFO` structure, while SignPath Foundation requires signed PE files to contain product-name and product-version metadata and requires those values to be enforced in the artifact configuration. ([GitHub][4])

Keep `src/app.rc` for the icon and create `src/version.rc.in`:

```rc
#include <windows.h>

VS_VERSION_INFO VERSIONINFO
 FILEVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
 PRODUCTVERSION @PROJECT_VERSION_MAJOR@,@PROJECT_VERSION_MINOR@,@PROJECT_VERSION_PATCH@,0
 FILEFLAGSMASK 0x3fL
#ifdef _DEBUG
 FILEFLAGS 0x1L
#else
 FILEFLAGS 0x0L
#endif
 FILEOS 0x40004L
 FILETYPE 0x1L
 FILESUBTYPE 0x0L
BEGIN
    BLOCK "StringFileInfo"
    BEGIN
        BLOCK "040904B0"
        BEGIN
            VALUE "CompanyName",      "ADV-Explorer contributors\0"
            VALUE "FileDescription",  "ADV-Explorer\0"
            VALUE "FileVersion",      "@PROJECT_VERSION@\0"
            VALUE "InternalName",     "adv-explorer\0"
            VALUE "LegalCopyright",   "ADV-Explorer contributors\0"
            VALUE "OriginalFilename", "adv-explorer.exe\0"
            VALUE "ProductName",      "ADV-Explorer\0"
            VALUE "ProductVersion",   "@PROJECT_VERSION@\0"
        END
    END

    BLOCK "VarFileInfo"
    BEGIN
        VALUE "Translation", 0x0409, 1200
    END
END
```

Then replace the existing Windows resource portion of `CMakeLists.txt`:

```cmake
if(WIN32)
    list(APPEND APP_SOURCES src/app.rc)
endif()
```

with:

```cmake
if(WIN32)
    configure_file(
        src/version.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
        @ONLY
    )

    list(APPEND APP_SOURCES
        src/app.rc
        "${CMAKE_CURRENT_BINARY_DIR}/version.rc"
    )
endif()
```

After building, verify the metadata:

```powershell
(Get-Item build/Release/adv-explorer.exe).VersionInfo |
    Format-List ProductName, ProductVersion, FileVersion, OriginalFilename
```

Expected output:

```text
ProductName      : ADV-Explorer
ProductVersion   : 0.1.4
FileVersion      : 0.1.4
OriginalFilename : adv-explorer.exe
```

### 2. Add the required code-signing policy

The current README contains no “Code signing policy” section and no project privacy declaration. SignPath requires both. ([GitHub][2])

Add this near the end of `README.md`:

```markdown
## Code signing policy

Free code signing provided by SignPath.io, certificate by SignPath Foundation.

### Team roles

- Committers and reviewers: [@sschwindt](https://github.com/sschwindt)
- Approvers: [@sschwindt](https://github.com/sschwindt)

### Privacy

This program will not transfer any information to other networked systems
unless specifically requested by the user or the person installing or
operating it.
```

The same section or a clear link to it should also appear on the documentation/download page.

### 3. Correct the developer documentation

The present developer guide describes both PFX signing and SignPath in the same section, which implies SignPath might supply a PFX certificate. It does not. The Foundation workflow is remote signing through SignPath’s service. ([GitHub][5])

Replace the current PFX-oriented SignPath explanation with something like:

```rst
Windows code signing
--------------------

Release builds can be signed through SignPath.io's free open-source program.
SignPath Foundation does not provide an exportable PFX file or private key.
The private key remains protected by SignPath, and GitHub Actions submits the
unsigned Windows build to SignPath for signing.

Signing requires project acceptance by SignPath Foundation and manual approval
of every release signing request. Builds produced without an approved SignPath
configuration remain unsigned.
```

Remove references to these values once the SignPath integration is active:

```text
WINDOWS_CERT_PFX_BASE64
WINDOWS_CERT_PASSWORD
```

## SignPath artifact configuration for this repository

Your Windows package contains:

```text
adv-explorer.exe
Qt6Core.dll
Qt6Gui.dll
Qt6Widgets.dll
...
templates/
README.md
LICENSE
```

Only `adv-explorer.exe` is your application binary. The Qt DLLs are upstream binaries and must **not** be signed using the ADV-Explorer SignPath policy. SignPath allows upstream unsigned binaries to be included, but project certificates must only sign the project’s own binaries. ([signpath.org][3])

Upload `dist/adv-explorer/` as the GitHub artifact. GitHub wraps that directory in a ZIP, so the artifact configuration root must be `<zip-file>`. ([SignPath.io][6])

Use:

```xml
<?xml version="1.0" encoding="utf-8"?>
<artifact-configuration
    xmlns="http://signpath.io/artifact-configuration/v1">

  <parameters>
    <parameter name="version" required="true" />
  </parameters>

  <zip-file>
    <pe-file
        path="adv-explorer.exe"
        product-name="ADV-Explorer"
        product-version="${version}"
        file-version="${version}"
        original-filename="adv-explorer.exe">

      <authenticode-sign
          description="ADV-Explorer"
          description-url="https://github.com/sschwindt/ADV-Explorer" />

    </pe-file>
  </zip-file>

</artifact-configuration>
```

SignPath can generate an initial artifact configuration from a sample artifact. Review that generated configuration carefully and remove any signing instructions it creates for Qt or other third-party DLLs. ([SignPath.io][7])

## Replacement Windows GitHub Actions job

Replace the current Windows job with the following structure after SignPath accepts the project:

```yaml
windows:
  runs-on: windows-2022

  permissions:
    actions: read
    contents: read

  steps:
    - uses: actions/checkout@v4

    - name: Install Qt 6
      uses: jurplel/install-qt-action@v4
      with:
        version: '6.7.*'
        arch: win64_msvc2019_64

    - name: Configure
      run: >
        cmake -B build
        -DCMAKE_BUILD_TYPE=Release
        -G "Visual Studio 17 2022"
        -A x64

    - name: Build
      run: cmake --build build --config Release

    - name: Test
      run: ctest --test-dir build -C Release --output-on-failure

    - name: Read version
      id: version
      shell: pwsh
      run: |
        $version = "${{ github.ref_name }}"

        if ($version.StartsWith("v")) {
          $version = $version.Substring(1)
        }

        "version=$version" >> $env:GITHUB_OUTPUT

    - name: Check executable metadata
      shell: pwsh
      run: |
        $info = (Get-Item "build/Release/adv-explorer.exe").VersionInfo

        $info |
          Format-List ProductName, ProductVersion, FileVersion, OriginalFilename

        if ($info.ProductName -ne "ADV-Explorer") {
          throw "Unexpected ProductName: $($info.ProductName)"
        }

        if ($info.OriginalFilename -ne "adv-explorer.exe") {
          throw "Unexpected OriginalFilename: $($info.OriginalFilename)"
        }

        if (startsWith("${{ github.ref }}", "refs/tags/v") -and
            $info.ProductVersion -ne "${{ steps.version.outputs.version }}") {
          throw "ProductVersion does not match the release tag."
        }

    - name: Prepare portable Windows directory
      shell: pwsh
      run: |
        New-Item `
          -ItemType Directory `
          -Force `
          -Path dist/adv-explorer/templates |
          Out-Null

        Copy-Item `
          build/Release/adv-explorer.exe `
          dist/adv-explorer/

        Copy-Item `
          templates/ADV-profiles.xlsx `
          dist/adv-explorer/templates/

        Copy-Item `
          LICENSE,README.md `
          dist/adv-explorer/

        windeployqt `
          --release `
          --no-translations `
          dist/adv-explorer/adv-explorer.exe

    - name: Upload unsigned artifact for SignPath
      id: upload-unsigned-windows
      if: startsWith(github.ref, 'refs/tags/v')
      uses: actions/upload-artifact@v4
      with:
        name: adv-explorer-windows-unsigned
        path: dist/adv-explorer/
        if-no-files-found: error

    - name: Submit SignPath signing request
      if: startsWith(github.ref, 'refs/tags/v')
      uses: signpath/github-action-submit-signing-request@v2
      with:
        api-token: ${{ secrets.SIGNPATH_API_TOKEN }}
        organization-id: ${{ vars.SIGNPATH_ORGANIZATION_ID }}
        project-slug: ${{ vars.SIGNPATH_PROJECT_SLUG }}
        signing-policy-slug: ${{ vars.SIGNPATH_SIGNING_POLICY_SLUG }}
        artifact-configuration-slug: windows-portable
        github-artifact-id: >-
          ${{ steps.upload-unsigned-windows.outputs.artifact-id }}
        wait-for-completion: true
        wait-for-completion-timeout-in-seconds: 3600
        output-artifact-directory: signed/adv-explorer
        parameters: |
          version: ${{ toJSON(steps.version.outputs.version) }}

    - name: Verify SignPath signature
      if: startsWith(github.ref, 'refs/tags/v')
      shell: pwsh
      run: |
        $exe = "signed/adv-explorer/adv-explorer.exe"

        if (-not (Test-Path $exe)) {
          throw "SignPath did not return adv-explorer.exe."
        }

        $signature = Get-AuthenticodeSignature $exe
        $signature | Format-List *

        if ($signature.Status -ne "Valid") {
          throw "Invalid Authenticode signature: $($signature.Status)"
        }

    - name: Create distributable ZIP
      shell: pwsh
      run: |
        if ("${{ startsWith(github.ref, 'refs/tags/v') }}" -eq "true") {
          $source = "signed/adv-explorer"
        }
        else {
          $source = "dist/adv-explorer"
        }

        Compress-Archive `
          -Path $source `
          -DestinationPath adv-explorer-windows-x86_64.zip

    - name: Upload final Windows package
      uses: actions/upload-artifact@v4
      with:
        name: adv-explorer-windows
        path: adv-explorer-windows-x86_64.zip
        if-no-files-found: error
```

SignPath’s GitHub integration requires the unsigned build to be uploaded as a GitHub artifact first. The signing action then uses the artifact ID, verifies the GitHub workflow origin, waits for approval, and downloads the signed result. For Foundation projects, all preceding jobs must use GitHub-hosted runners, which this repository already does. ([SignPath.io][6])

### Minor PowerShell correction

This portion of the example:

```powershell
if (startsWith("${{ github.ref }}", "refs/tags/v") -and ...)
```

is not valid PowerShell. Use:

```powershell
$isTag = "${{ github.ref }}".StartsWith("refs/tags/v")

if ($isTag -and
    $info.ProductVersion -ne "${{ steps.version.outputs.version }}") {
    throw "ProductVersion does not match the release tag."
}
```

Put that corrected form into the metadata-check step.

## Update the release job

The workflow currently downloads every artifact with `merge-multiple: true`. Once you add the unsigned SignPath artifact, that would also download the unsigned directory into the release job. It would probably not be published, but it is unnecessary and error-prone. The current release job behavior is visible in the repository workflow. ([GitHub][1])

Use explicit artifact downloads:

```yaml
release:
  if: startsWith(github.ref, 'refs/tags/v')
  needs: [linux, windows]
  runs-on: ubuntu-24.04

  permissions:
    contents: write

  steps:
    - name: Download Linux package
      uses: actions/download-artifact@v4
      with:
        name: adv-explorer-linux
        path: release

    - name: Download signed Windows package
      uses: actions/download-artifact@v4
      with:
        name: adv-explorer-windows
        path: release

    - name: Create release
      uses: softprops/action-gh-release@v2
      with:
        files: |
          release/adv-explorer-linux-x86_64.AppImage
          release/adv-explorer-windows-x86_64.zip
```

## Secrets and variables to configure

After acceptance, configure:

### GitHub secret

```text
SIGNPATH_API_TOKEN
```

### GitHub repository variables

```text
SIGNPATH_ORGANIZATION_ID
SIGNPATH_PROJECT_SLUG
SIGNPATH_SIGNING_POLICY_SLUG
```

Delete or stop using:

```text
WINDOWS_CERT_PFX_BASE64
WINDOWS_CERT_PASSWORD
```

## Recommended sequence

1. Add the Windows `VERSIONINFO` resource.
2. Add the README code-signing policy and privacy declaration.
3. Correct `docs/developer.rst`.
4. Publish at least one new unsigned release containing the corrected metadata.
5. Build external evidence of project use and reputation.
6. Apply to SignPath Foundation.
7. After acceptance, create the SignPath artifact configuration.
8. Add the SignPath GitHub workflow and repository token.
9. Tag a release.
10. Manually approve the signing request in SignPath.
11. Verify that the GitHub release contains the signed executable.

## Bottom line

**Technical suitability:** yes.

**Ready for SignPath application today:** not quite.

**Required code changes:** straightforward.

**Most likely obstacle:** the repository is extremely new and currently has little publicly verifiable reputation. SignPath may accept a niche scientific application when backed by credible institutional, research, documentation, and user evidence, but the repository statistics alone are weak.

[1]: https://github.com/sschwindt/ADV-Explorer/blob/main/.github/workflows/build.yml "ADV-Explorer/.github/workflows/build.yml at main · sschwindt/ADV-Explorer · GitHub"
[2]: https://github.com/sschwindt/ADV-Explorer "GitHub - sschwindt/ADV-Explorer · GitHub"
[3]: https://signpath.org/terms.html?utm_source=chatgpt.com "SignPath Foundation conditions for Open Source projects"
[4]: https://raw.githubusercontent.com/sschwindt/ADV-Explorer/main/CMakeLists.txt "raw.githubusercontent.com"
[5]: https://raw.githubusercontent.com/sschwindt/ADV-Explorer/main/docs/developer.rst "raw.githubusercontent.com"
[6]: https://docs.signpath.io/trusted-build-systems/github?utm_source=chatgpt.com "GitHub"
[7]: https://docs.signpath.io/artifact-configuration/?utm_source=chatgpt.com "Artifact Configuration"

