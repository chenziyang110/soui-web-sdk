# Historical source regression inputs

These are exact retained pre-fix source and captured compiler database bytes.
The manifest pins every input. Their historical paths are data used to reproduce
parsing failures; tests load only these repository-relative files and never run
the historical commands or access their paths. Source/model regressions do not
stand in for actual native compilation, lifecycle or release acceptance.
