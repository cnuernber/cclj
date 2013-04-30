use strict;
use File::Basename;
use File::Spec::Functions;
use XML::LibXML;
use Data::UUID;
use Cwd qw(realpath getcwd);

my $om;
my $xpj;

sub config_type_to_vs_type {
	my $configType = shift;
	if ( $configType eq "static_library" ) {
		return "StaticLibrary";
	}
	elsif( $configType eq "dynamic_library" ) {
		return "DynamicLibrary";
	}
	else {
		return "Executable";
	}
}

sub dash_to_camel
{
	my $name = shift;
	my $retval = "";
	my $nextUpper = 1;
	my @nameArray = split( '', $name );
	foreach my $char (@nameArray) {
		if ( $nextUpper ) {
			$retval = $retval . uc($char);
			$nextUpper = 0;
		}
		elsif( $char eq "-" ) {
			$nextUpper = 1;
		}
		else {
			$retval = $retval . $char;
		}
	}
	return $retval;
}

sub print_property 
{
	my ($file, $target, $configName, $propName) = @_;
	my $propEntry = $om->{compilation_properties}->{$propName};
	my $propertyType = $propEntry->{type};
	my $camelValue = $propertyType eq "set";
	my $propValue = $om->get_target_property( $target, $configName, $propName );
	$propName = dash_to_camel($propName);
	if ( $propertyType eq "set" ) {
		$propValue = dash_to_camel($propValue);
	}
	print $file "    <$propName>$propValue</$propName>\n";
}

sub print_property_itemdef
{
	my ($file, $target, $configName, $propName) = @_;
	print $file "  ";
	print_property( $file, $target, $configName, $propName );
}

sub to_semi_delim {
	my $list = shift;
	my $retval = "";
	
	foreach my $item (@$list) {
		if ( length($retval) ) {
			$retval = $retval . ';';
		}
		$retval = $retval . $item;
	}
	return $retval;
}

sub to_semi_delim_list_of_lists
{
	my $listoflists = shift;
	my $retval = "";
	foreach my $list (@$listoflists) {
		foreach my $item (@$list) {
			if ( length($retval) ) {
				$retval = $retval . ';';
			}
			$retval = $retval . $item;
		}
	}
	return $retval;
}


sub to_space_delim_list_of_lists
{
	my $listoflists = shift;
	my $retval = "";
	foreach my $list (@$listoflists) {
		foreach my $item (@$list) {
			if ( length($retval) ) {
				$retval = $retval . ' ';
			}
			$retval = $retval . $item;
		}
	}
	return $retval;
}
my $FILE_TYPE_COMPILE = 1;
my $FILE_TYPE_HEADER = 2;
my $FILE_TYPE_UNKNOWN = 3;

sub get_file_type
{
	my $file = shift;
	if ( $file =~ /\.cpp$|\.c$|\.cc$|\.cxx$|\.def$|odl|\.idl$|\.hpj$|\.bat$|\.asm$|\.asmx$/ ) {
		return $FILE_TYPE_COMPILE;
	}
	elsif ( $file =~ /\.h$|\.hpp$|\.hxx$|\.hm$|\.inl$|\.inc$|\.xsd$/ ) {
		return $FILE_TYPE_HEADER;
	}
	else {
		return $FILE_TYPE_UNKNOWN;
	}
}

my $enable_exceptions_map = { "false"=>"false", "true"=>"Sync", "true-with-c"=>"SyncWithC", "true-with-SEH"=>"Async" };
my $struct_member_alignment_map = { "default"=>"Default", "1"=>"1Byte", "2"=>"2Bytes", "4"=>"4Bytes", "8"=>"8Bytes", "16"=>"16Bytes" };
my $warning_level_map = { "0"=>"TurnOffAllWarnings", "1"=>"Level1", "2"=>"Level2", "3"=>"Level3", "4"=>"Level4", "5"=>"EnableAllWarnings" };
my $configuration_type_map = { "static-library"=>"StaticLibrary", "dynamic-library"=>"DynamicLibrary", "executable" => "Application", "console-executable"=>"Application" };

sub safe_lookup 
{
	my ($hashKey, $hash, $hashname) = @_;
	my $retval = $hash->{$hashKey};
	if ( !$retval ) {
		die "Unrecognized key $hashKey for hash $hashname";
	}
	return $retval;
}

sub process
{
	$om = shift;
	$xpj = shift;
	my $projects = $om->get_projects();
	my $uidgen = Data::UUID->new();
	my $platform = $xpj->{platform};
	foreach my $proj (@$projects) {
		my $projdir = $proj->{project_directory};
		my $projname = $proj->{name};
		my $projfilename = catfile( $projdir, "$projname.sln" );
		open( my $projfile, ">", $projfilename ) || die ("Unable to open solution file $projfilename" );
		binmode $projfile; # drop all PerlIO layers possibly created by a use open pragma
		print $projfile "\r\n";
		print $projfile "Microsoft Visual Studio Solution File, Format Version 12.00\r\n";
		print $projfile "# Visual Studio 2012\r\n";

		my $targets = $proj->{targets};
		my $confList = [];
		my $confHash = {};


		#first output the solution file

		foreach my $target (@$targets) {
			my $targetId = $target->{uid};
			my $targetName = $target->{name};
			my $crapid = $uidgen->to_string( $uidgen->create() );
			my $targetConfigs = $om->get_configuration_names( $target );
			foreach my $targetConf (@$targetConfigs) {
				if ( !$confHash->{$targetConf} ) {
					push( @$confList, $targetConf );
					$confHash->{$targetConf} = 1;
				}
			}
			print $projfile "Project(\"{$crapid}\") = \"$targetName\", \"$targetName.vcxproj\", \"{$targetId}\"\r\n";
			print $projfile "EndProject\r\n";
		}
		print $projfile "Global\r\n";
		print $projfile "\tGlobalSection(SolutionConfigurationPlatforms) = preSolution\r\n";
		foreach my $conf (@$confList) {
			my $confName = "$conf|$platform";
			print $projfile "\t\t$confName = $confName\r\n";
		}
		print $projfile "\tEndGlobalSection\r\n";
		
		
		print $projfile "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\r\n";
		foreach my $target (@$targets) {
			my $targetId = $target->{uid};
			my $targetConfigs = $om->get_configuration_names( $target );
			foreach my $targetConf (@$targetConfigs) {
				my $confName = "$targetConf|$platform";
				print $projfile "\t\t{$targetId}.$confName.ActiveCfg = $confName\r\n";
				print $projfile "\t\t{$targetId}.$confName.Build.0 = $confName\r\n";
			}
		}
		print $projfile "\tEndGlobalSection\r\n";
		print $projfile "\tGlobalSection(SolutionProperties) = preSolution\r\n";
		print $projfile "\t\tHideSolution = FALSE\r\n";
		print $projfile "\tEndGlobalSection\r\n";
		print $projfile "EndGlobal\r\n";
		close $projfile;

		#now output each target in the project
		
		foreach my $target (@$targets) {
			my $targetId = $target->{uid};
			my $targetName = $target->{name};
			my $targetProjName = catfile( $projdir, "$targetName.vcxproj" );
			my $targetFilterName = "$targetProjName.filters";
			open( my $targetProj, ">", $targetProjName ) || die "unable to open target file $targetProjName";
			open( my $targetFilter, ">", $targetFilterName ) || die "unable to open target filter $targetFilterName";
			print $targetProj "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";

			print $targetProj 
				"<Project DefaultTargets=\"Build\" ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
			print $targetProj "  <ItemGroup Label=\"ProjectConfigurations\">\n";
			my $targetConfigs = $om->get_configuration_names( $target );
			foreach my $conf (@$targetConfigs) {
				my $confName = "$conf|$platform";
				print $targetProj <<END;
    <ProjectConfiguration Include="$confName">
      <Configuration>$conf</Configuration>
      <Platform>$platform</Platform>
    </ProjectConfiguration>
END
			}
			print $targetProj "  </ItemGroup>\n";

			#ask the om to merge all of the files into one array of unique names.

			print $targetProj "  <ItemGroup>\n";
			my $targetFiles = $om->get_target_files( $target );
			foreach my $file (@$targetFiles) {
				my $filetype = get_file_type( $file );
				if ( $filetype == $FILE_TYPE_COMPILE ) {
					print $targetProj "    <ClCompile Include=\"$file\" />\n";
				}
				elsif ( $filetype == $FILE_TYPE_HEADER ) {
					print $targetProj "    <ClInclude Include=\"$file\" />\n";
				}
				else {
					print $targetProj "    <None Include=\"$file\" />\n";
				}
			}
			print $targetProj "  </ItemGroup>\n";
			my $projectType = $platform . "Proj";
			print $targetProj <<END;
  <PropertyGroup Label="Globals">
    <ProjectGuid>{$targetId}</ProjectGuid>
    <Keyword>$projectType</Keyword>
    <RootNamespace>$targetName</RootNamespace>
  </PropertyGroup>				
END
			print $targetProj "  <Import Project=\"\$(VCTargetsPath)\\Microsoft.Cpp.Default.props\" />\n";

			foreach my $conf (@$targetConfigs) {
				my $confName = "$conf|$platform";
				my $cond = '$(Configuration)|$(Platform)';
				my $configType = safe_lookup( $om->get_target_property( $target, $conf, "configuration-type" ), $configuration_type_map, "vc12-configuration-type" );
				my $artifactName = $om->get_target_property( $target, $conf, "artifact-name" );
				my ($targetName, $targetExt) = $artifactName =~ /([^\.]*)(\..*)$/;
				my $outDir = $proj->{binary_directory};
				if ( $configType eq "StaticLibrary" ) {
					$outDir = $proj->{library_directory};
				}
				my $buildDir = catdir( $proj->{build_directory}, $conf );
				
				print $targetProj "  <PropertyGroup Condition=\"'$cond'=='$confName'\" Label=\"Configuration\">\n";
				print $targetProj "    <PlatformToolset>v110</PlatformToolset>\n";
				print $targetProj "    <ConfigurationType>$configType</ConfigurationType>\n";
				print_property( $targetProj, $target, $conf, "use-debug-libraries" );
				print_property( $targetProj, $target, $conf, "whole-program-optimization" );
				print_property( $targetProj, $target, $conf, "character-set" );
				print $targetProj "    <OutDir>$outDir\\</OutDir>\n";
				print $targetProj "    <IntDir>$buildDir\\</IntDir>\n";
				print $targetProj "    <TargetName>$targetName</TargetName>\n";
				print $targetProj "    <TargetExt>$targetExt</TargetExt>\n";
				print $targetProj "  </PropertyGroup>\n";
			}
			print $targetProj "  <Import Project=\"\$(VCTargetsPath)\\Microsoft.Cpp.props\" />\n";
			foreach my $conf (@$targetConfigs) {
				my $confName = "$conf|$platform";
				print $targetProj <<END;
  <ImportGroup Label="PropertySheets" Condition="'\$(Configuration)|\$(Platform)'=='$confName'">
    <Import Project="\$(UserRootDir)\\Microsoft.Cpp.\$(Platform).user.props" Condition="exists('\$(UserRootDir)\\Microsoft.Cpp.\$(Platform).user.props')" Label="LocalAppDataPlatform" />
  </ImportGroup>
END
			}
			foreach my $conf (@$targetConfigs) {
				my $confName = "$conf|$platform";
				my $headerlist = to_semi_delim( $om->get_path_list( $target, $conf, "header" ) );
				my $linkerlist = to_semi_delim( $om->get_path_list( $target, $conf, "linker" ) );
				my $preprocessor = to_semi_delim_list_of_lists( 
					$om->get_target_and_config_nodes_by_type( $target, $conf, "preprocessor" ) );
				my $cflags = to_space_delim_list_of_lists( $om->get_target_and_config_nodes_by_type( $target, $conf, "cflags" ) );
				my $lflags = to_space_delim_list_of_lists( $om->get_target_and_config_nodes_by_type( $target, $conf, "lflags" ) );
				my $level = safe_lookup( $om->get_target_property( $target, $conf, "warning-level" ), $warning_level_map, "vc12-warning-level" );
				my $rtti = $om->get_target_property( $target, $conf, "enable-rtti" );
				my $exceptions = safe_lookup( $om->get_target_property( $target, $conf, "enable-exceptions" ), $enable_exceptions_map, "vc12-enable-exceptions" );
				my $sma = safe_lookup( $om->get_target_property( $target, $conf, "struct-member-alignment" ), $struct_member_alignment_map, "vc12-struct-member-alignment" );
				my $confType = $om->get_target_property($target,$conf,"configuration-type");
				my $subsystem = "Windows";
				if ( $confType eq "console-executable" ) {
					$subsystem = "Console";
				}
				
				print $targetProj "  <ItemDefinitionGroup Condition=\"'\$(Configuration)|\$(Platform)'=='$confName'\">\n";
				print $targetProj "    <ClCompile>\n";
				print $targetProj "      <WarningLevel>$level</WarningLevel>\n";
				print_property_itemdef( $targetProj, $target, $conf, "optimization" );
				print $targetProj "      <PreprocessorDefinitions>$preprocessor;%(PreprocessorDefinitions)</PreprocessorDefinitions>\n";
				print $targetProj "      <AdditionalIncludeDirectories>$headerlist;%(AdditionalIncludeDirectories)</AdditionalIncludeDirectories>\n";
				print_property_itemdef( $targetProj, $target, $conf, "multi-processor-compilation" );
				print_property_itemdef( $targetProj, $target, $conf, "minimal-rebuild" );
				print_property_itemdef( $targetProj, $target, $conf, "enable-enhanced-instruction-set" );
				print_property_itemdef( $targetProj, $target, $conf, "floating-point-model" );
				print_property_itemdef( $targetProj, $target, $conf, "runtime-library" );
				print_property_itemdef( $targetProj, $target, $conf, "intrinsic-functions" );
				print_property_itemdef( $targetProj, $target, $conf, "string-pooling" );
				print_property_itemdef( $targetProj, $target, $conf, "buffer-security-check" );
				print $targetProj "      <StructMemberAlignment>$sma</StructMemberAlignment>\n";
				print $targetProj "      <ProgramDataBaseFileName>\$(OutDir)\$(TargetName).pdb</ProgramDataBaseFileName>\n";
				print $targetProj "      <AdditionalOptions>$cflags %(AdditionalOptions)</AdditionalOptions>\n";
				print $targetProj "      <ExceptionHandling>$exceptions</ExceptionHandling>\n";
				print $targetProj "      <RuntimeTypeInfo>$rtti</RuntimeTypeInfo>\n";
				print $targetProj "    </ClCompile>\n";
				print $targetProj "    <Link>\n";
				print $targetProj "      <SubSystem>$subsystem</SubSystem>\n";
				print_property_itemdef( $targetProj, $target, $conf, "generate-debug-information" );
				print $targetProj "    </Link>\n";
				print $targetProj "  </ItemDefinitionGroup>\n";

			}
			print $targetProj "  <Import Project=\"\$(VCTargetsPath)\\Microsoft.Cpp.targets\" />\n";
			print $targetProj "</Project>\n";
			close $targetProj;
			
			print $targetFilter "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";
			print $targetFilter "<Project ToolsVersion=\"4.0\" xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n";
			print $targetFilter "  <ItemGroup>\n";
			my $groups = $target->{filegroups};
			my @groupNames = keys %$groups;
			my $actualGroups = {};
			my $actualFiles = {};
			my $uuidgen = new Data::UUID;
			foreach my $groupName (@groupNames) {
				my $filegroup = $groups->{$groupName};
				my $fileHash = $filegroup->{files};
				my $name = $filegroup->{name};
				my $groupRoot = $filegroup->{root};
				my $root = realpath( catdir( $projdir, $filegroup->{root} ) );
				my @files = sort( keys %$fileHash );
				foreach my $file (@files) {
					my $fullpath = realpath( catfile( $projdir, $file ) );
					if ( !$actualFiles->{$fullpath} ) {
						$actualFiles->{$fullpath} = 1;
						my $relpath = File::Spec->abs2rel( $fullpath, $root );
						my @pathParts = split( /\\|\//, $relpath );
						#remove the actual file from pathParts
						pop(@pathParts);

						#if name exists, then name is at the head of path parts
						if ( length( $name ) ) {
							unshift( @pathParts, $name );
						}
						my $chopLen = $#pathParts;
						my $filterName = "";
						foreach my $part (@pathParts) {
							if ( length($filterName) ) {
								$filterName = $filterName . "\\";
							}
							$filterName = $filterName . $part;
							if ( !$actualGroups->{$filterName} ) {
								$actualGroups->{$filterName} = 1;
								my $filterId = $uuidgen->to_string( $uuidgen->create() );
								print $targetFilter "    <Filter Include=\"$filterName\">\n";
								print $targetFilter "      <UniqueIdentifier>{$filterId}</UniqueIdentifier>\n";
								print $targetFilter "    </Filter>\n";
							}
						}	
						my $filetype = get_file_type( $file );
						my $xmltag = "";
						if ( $filetype == $FILE_TYPE_COMPILE ) {
							$xmltag = "ClCompile";
						}
						elsif( $filetype == $FILE_TYPE_HEADER ) {
							$xmltag = "ClInclude";
						}
						else {
							$xmltag = "None";
						}   
						print $targetFilter "    <$xmltag Include=\"$file\">\n";
						print $targetFilter "      <Filter>$filterName</Filter>\n";
						print $targetFilter "    </$xmltag>\n";
					}
				}
			}
			print $targetFilter "  </ItemGroup>\n";
			print $targetFilter "</Project>\n";
			close $targetFilter;
		}
	}
}



my $compilation_properties = {
	"use-debug-libraries" => { type=>"boolean", default=>"true" },
	"character-set" => { type=>"set", values=>["unicode", "multibyte"], default=>"unicode" },
	"multi-processor-compilation" => { type=>"boolean", default=>"true" },
	"minimal-rebuild" => { type=>"boolean", default=>"false" },
	"function-level-linking" => { type=>"boolean", default=>"false" },
	"enable-comdat-folding" => { type=>"boolean", default=>"false" },
	"optimize-references" => { type=>"boolean", default=>"false" },
	"enable-exceptions" => { type=>"set", values=>["false", "true", "true-with-c", "true-with-SEH"], default=>"true-with-SEH" },
	"enable-enhanced-instruction-set" => { type=>"set", values=>["no-extensions", "advanced-vector-extensions", "streaming-SIMD-extensions-2", "streaming-SIMD-extensions"], default=>"no-extensions" },
	"runtime-library" => { type=>"set", values=>["multi-threaded-DLL", "multi-threaded-debug-DLL", "multi-threaded", "multi-threaded-debug"], default=>"multi-threaded-debug-dll" },
};


return { process=>\&process, extended_compilation_properties=>$compilation_properties };
