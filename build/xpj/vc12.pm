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
			$confName = "$conf|$platform";
			print $projfile "\t\t$confName = $confName\r\n";
		}
		print $projfile "\tEndGlobalSection\r\n";
		
		
		print $projfile "\tGlobalSection(ProjectConfigurationPlatforms) = postSolution\r\n";
		foreach my $target (@$targets) {
			my $targetId = $target->{uid};
			my $targetConfigs = $om->get_configuration_names( $target );
			foreach my $targetConf (@$targetConfigs) {
				$confName = "$targetConf|$platform";
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
			print $targetFilter "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n";

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
				if ( $file =~ /\.cpp$|\.c$|\.cc$|\.cxx$|\.def$|odl|\.idl$|\.hpj$|\.bat$|\.asm$|\.asmx$/ ) {
					print $targetProj "    <ClCompile Include=\"$file\" />\n";
				}
				elsif ( $file =~ /\.h$|\.hpp$|\.hxx$|\.hm$|\.inl$|\.inc$|\.xsd$/ ) {
					print $targetProj "    <ClInclude Include=\"$file\" />\n";
				}
				else {
					print $targetProj "    <None Include=\"$file\" />\n";
				}
			}
			print $targetProj "  </ItemGroup>\n";
			$projectType = $platform . "Proj";
			print $targetProj <<END;
  <PropertyGroup Label="Globals">
    <ProjectGuid>{$targetId}</ProjectGuid>
    <Keyword>$projectType</Keyword>
    <RootNamespace>$targetName</RootNamespace>
  </PropertyGroup>				
END
			my $projPath = '$(VCTargetsPath)';
			print $targetProj "  <Import Project=\"$projPath\\Microsoft.Cpp.Default.props\" />\n";

			foreach my $conf (@$targetConfigs) {
				my $confName = "$conf|$platform";
				my $cond = '$(Configuration)|$(Platform)';
				print $targetProj "  <PropertyGroup Condition=\"$cond=='$confName'\" Label=\"Configuration\">\n";
				print $targetProj "    <PlatformToolset>vc110</PlatformToolset>\n";
				print_property( $targetProj, $target, $conf, "configuration-type" );
				print_property( $targetProj, $target, $conf, "use-debug-libraries" );
				print_property( $targetProj, $target, $conf, "whole-program-optimization" );
				print_property( $targetProj, $target, $conf, "character-set" );
				print $targetProj "  </PropertyGroup>\n";
			}
			print $targetProj "</Project>\n";
			close $targetProj;
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
};


return { process=>\&process, extended_compilation_properties=>$compilation_properties };
