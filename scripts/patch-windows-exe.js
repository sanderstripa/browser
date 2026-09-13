const fs = require("fs");
const ResEdit = require("resedit");

const [, , sourcePath, iconPath, outputPath] = process.argv;
if (!sourcePath || !iconPath || !outputPath) {
  throw new Error("Usage: node patch-windows-exe.js <source.exe> <icon.ico> <output.exe>");
}

const executable = ResEdit.NtExecutable.from(fs.readFileSync(sourcePath), { ignoreCert: true });
const resources = ResEdit.NtExecutableResource.from(executable);
const iconFile = ResEdit.Data.IconFile.from(fs.readFileSync(iconPath));
const iconGroups = ResEdit.Resource.IconGroupEntry.fromEntries(resources.entries);
const iconGroup = iconGroups[0];

if (!iconGroup) throw new Error("The executable has no icon group to replace");

ResEdit.Resource.IconGroupEntry.replaceIconsForResource(
  resources.entries,
  iconGroup.id,
  iconGroup.lang,
  iconFile.icons.map((item) => item.data)
);

const versionInfo = ResEdit.Resource.VersionInfo.fromEntries(resources.entries)[0];
if (versionInfo) {
  versionInfo.setFileVersion(0, 5, 1, 0, 1033);
  versionInfo.setProductVersion(0, 5, 1, 0, 1033);
  versionInfo.setStringValues(
    { lang: 1033, codepage: 1200 },
    {
      CompanyName: "Sander Stripa",
      FileDescription: "Internet Browser",
      InternalName: "Internet Browser",
      LegalCopyright: "© 2026 Sander Stripa",
      OriginalFilename: "Internet Browser.exe",
      ProductName: "Internet Browser",
      FileVersion: "0.5.1",
      ProductVersion: "0.5.1"
    }
  );
  versionInfo.outputToResourceEntries(resources.entries);
}

resources.outputResource(executable);
const generated = executable.generate();
fs.writeFileSync(outputPath, Buffer.from(generated));
