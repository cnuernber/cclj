package xpjpom;
# this section builds the object model that the backends process.


use strict;

use File::Basename;
use File::Spec::Functions;
use Cwd qw(realpath getcwd);
use XML::LibXML;
use File::Path;
use Data::UUID;

my $projdir;

sub elemText {
	my $elem = shift;
	my $retval = "";
	my @childNodes = $elem->childNodes;
	foreach my $child (@childNodes) {
		$retval = $retval . $child->data();
	}
	return $retval;
}

sub project_relative_dir {
	my $elem = shift;
	my $dir = catdir( $projdir, elemText( $elem ) );
	mkpath( $dir );
	return File::Spec::->abs2rel( realpath( $dir ), $projdir );
}

sub trim {
	my $input = shift;
	my ($output) = $input =~ /^\s*([^\s]?.*[^\s])\s*$/;
	return $output;
}

sub add_node {
	my $parent = shift;
	my $nodeType = shift;
	my $items = $parent->{nodes};
	if ( !$items ) {
		$items = [];
		$parent->{nodes} = $items;
	}
	my $newItem = { type => $nodeType, values => [] };
	push( @$items, $newItem );
	return $newItem;
}

sub add_array_node {
	my $parent = shift;
	my $nodeType = shift;
	my $newItem = add_node( $parent, $nodeType );
	my $retval = [];
	$newItem->{values} = $retval;
	return $retval;
}

sub add_hash_node {
	my $parent = shift;
	my $nodeType = shift;
	my $newItem = add_node( $parent, $nodeType );
	my $retval = {};
	$newItem->{values} = $retval;
	return $retval;
}

sub add_property_node {
	my $parent = shift;
	my $propName = shift;
	my $propValue = shift;
	my $nodeType = "property";
	my $newItem = add_hash_node( $parent, $nodeType );
	$newItem->{name} = $propName;
	$newItem->{value} = $propValue;
}

#all known properties by all backends must pass through this filter.  This is so we can alert the use for properties
#that do not apply to any backend in the system.  There should probably be a way to query the currently selected tool
#and allow it to extend this property set array.  Initial defaults are by convention for debug builds.
my $initial_compilation_properties = { 
	"configuration-type" => { type=>"set", values=>["static-library","dynamic-library","executable","console-executable"] },
	"whole-program-optimization" => { type=>"boolean", default=>"false" },
	"warning-level" => { type=>"set", values=>["0","1","2","3","4","5"], default=>"4" },
	"optimization" => { type=>"set", values=>["disabled", "max-speed", "min-space", "full"], default=>"disabled" },
	"generate-debug-information" => { type=>"boolean", default=>"true" },
	"intrinsic-functions" => { type=>"boolean", default=>"false" },
	"artifact-name" => { type=>"string" },
	"enable-rtti" => { type=>"boolean", default=>"true" },
	"enable-exceptions" => { type=>"boolean", default=>"true" },
	"floating-point-model" => { type=>"set", values=>["strict", "precise", "fast"], default=>"strict" },
	"string-pooling" => { type=>"boolean", default=>"true" }, #I don't see why you wouldn't enable this one by default.
	"buffer-security-check" => { type=>"boolean", default=>"true" },
	"struct-member-alignment" => { type=>"set", values=>["default", "1", "2", "4", "8", "16"], default=>"default" },
};

sub project_relative_file 
{
	my ($fname, $srcPath, $message) = @_;
	$fname = catfile( $srcPath, $fname );
	if ( !(-f $fname) ) {
		die "$message: $fname doesn't exist or isn't a file";
	}
	$fname = realpath( $fname );
	return File::Spec->abs2rel( $fname, $projdir );
}

sub process_target_config_child {
	my $om = shift;
	my $parent = shift;
	my $elem = shift;
	my $nodeName = $elem->nodeName;
	my $lineno = $elem->line_number();
	if( $nodeName eq "search" ) {
		my $searchType = $elem->getAttribute( "type" );
		die "Unrecognized search type $searchType" unless( $searchType eq "header" || $searchType eq "linker" );
		my $arrayName = "$searchType-search-paths";
		my $resultArray = add_array_node( $parent, $arrayName );
		
		my @lines = split( /\n/, elemText( $elem ) );
		foreach my $line (@lines ) {
			my $trimmed = trim( $line );
			if ( $trimmed ) {
				my $fullpath = catdir( getcwd(), $trimmed );
				if ( !( -e $fullpath ) ) {
					die "Search path $line does not exist";
				}
				my $fullpath = realpath( $fullpath );
				my $relpath = File::Spec->abs2rel( $fullpath, $projdir );
				push( @$resultArray, $relpath );
			}
		}
	}
	elsif( $nodeName eq "cflags" 
		   || $nodeName eq "lflags" 
		   || $nodeName eq "preprocessor" 
		   || $nodeName eq "libraries" 
		   || $nodeName eq "cppflags" ) {
		my $resultArray = add_array_node( $parent, $nodeName );
		my @lines = split( /\n/, elemText( $elem ) );
		foreach my $line (@lines ) {
			my $trimmed = trim( $line );
			if ( $trimmed ) {
				push( @$resultArray, $trimmed );
			}
		}
	}
	elsif( $nodeName eq "precompiled-header" ) {
		my $root = $elem->getAttribute( "root" );
		my $header = $elem->getAttribute( "header" );
		my $source = $elem->getAttribute( "source" );
		my $srcPath = realpath( catdir( getcwd(), $root ) );
		if ( $header ) {
			$header = project_relative_file( $header, $srcPath, "precompiled-header" );
			if ( $source ) {
				$source = project_relative_file( $source, $srcPath, "precompiled-header" );
			}
			my $propvalue = { header=>$header, source=>$source };
			add_property_node( $parent, "precompiled-header", $propvalue );
		}
	}
	else {
		my $compilation_properties = $om->{compilation_properties};
		my $property = $compilation_properties->{$nodeName};
		if ( $property ) {
			my $proptype = $property->{type};
			my $propvalue = trim( elemText( $elem ) );
			if ( $proptype eq "boolean" ) {
				if ( lc($propvalue) eq "true" ) {
					$propvalue = "true";
				}
				elsif( lc($propvalue) eq "false" ) {
					$propvalue = "false";
				}
				else {
					die "Unrecognized boolean value for property $nodeName: $propvalue";
				}
				add_property_node( $parent, $nodeName, $propvalue );
			}
			elsif( $proptype eq "set" ) {
				my $legalValues = $property->{values};
				my $found = 0;
				foreach my $val (@$legalValues) {
					if ( !$found && $propvalue eq $val ) {
						$found = 1;
					}
				}
				if ( !$found ) {
					my @legalValueArray = @$legalValues;
					die "Unrecognized set value for property $nodeName: $propvalue [@legalValueArray]";
				}
				add_property_node( $parent, $nodeName, $propvalue );
			}
			elsif( $proptype eq "string" ) {
				add_property_node( $parent, $nodeName, $propvalue );
			}
			else {
				die "Unrecognized property type $proptype";
			}
		}
		else {
			return 0;
		}
	}
	return 1;
}

sub match_files_to_glob {
	my $rootdir = shift;
	my $globPattern = shift;
	my $retval = [];
	my @strdata = split( '', $globPattern );
	my $regex = "^";
	foreach my $char (@strdata ) {
		if ( $char eq '*' ) {
			$regex = $regex . ".*";
		}
		else {
			$regex = $regex . $char;
		}
	}
	$regex = $regex . "\$";
	opendir( my $dirhandle, $rootdir ) || die "Failed to open directory $rootdir\n";
	while( readdir( $dirhandle ) ) {
		my $fname = $_;
		if ( $fname eq "." || $fname eq ".." ) {
		}
		else {
			if ( $fname =~m/$regex/ ) {
				my $localfile = catfile( $rootdir, $_ );
				if ( -f $localfile ) {
					push( @$retval, $localfile );
				}
			}
		}
	}
	closedir( $dirhandle );
	return $retval;
}

sub process_project_children {
	my $om = shift;
	my $project = shift;
	my $projChild = shift;
	my $ug = new Data::UUID;
	my $name = $projChild->nodeName;
	if ( $name eq "projdir" ) {
		$projdir = catdir( getcwd(), elemText( $projChild ) );
		mkpath( $projdir );
		$projdir = realpath( $projdir );
		$project->{project_directory} = $projdir;
	}
	if ( $name eq "bindir" ) {
		$project->{binary_directory} = project_relative_dir( $projChild );
	}
	elsif ( $name eq "libdir" ) {
		$project->{library_directory} = project_relative_dir( $projChild );
	}
	elsif ( $name eq "builddir" ) {
		$project->{build_directory} = project_relative_dir( $projChild );
	}
	elsif( $name eq "target" ) {
		my $uid = $ug->to_string( $ug->create() );

		my $newTarget = { name => $projChild->getAttribute( "name" ), uid => $uid, filegroups => {}, project => $project, depends=> {} };
		my $targets = $project->{targets};
		push( @$targets, $newTarget );
		my @targetChildren = $projChild->childNodes;
		foreach my $targetChild (@targetChildren) {
			if ( $targetChild->nodeType == XML_ELEMENT_NODE ) { 
				
				my $nodeName = $targetChild->nodeName;
				if ( $nodeName eq "configuration" ) {
					#get or create config by name
					my $configName = $targetChild->getAttribute( "name" );
					my $configuration = add_hash_node( $newTarget, "configuration" );
					$configuration->{type} = $targetChild->getAttribute( "type" );
					$configuration->{name} = $configName;
					
					my @configChildren = $targetChild->childNodes;
					foreach my $configChild (@configChildren) {
						if ( $configChild->nodeType == XML_ELEMENT_NODE ) { 
							my $configChildNodeName = $configChild->nodeName;
							die "Unrecognized configuration child $configChildNodeName" 
								unless process_target_config_child( $om, $configuration, $configChild );
						}
					}
				}
				elsif( $nodeName eq "files" ) {
					my $fileGroupName = $targetChild->getAttribute( "name" );
					my $fileGroupRoot = $targetChild->getAttribute( "root" );
					my $actualRoot = catdir( getcwd(), $fileGroupRoot );
					if ( !( -e $actualRoot ) ) {
						die "Unable to find directory: $actualRoot";
					}
					$actualRoot = realpath( $actualRoot );
					$fileGroupRoot = File::Spec::->abs2rel( $actualRoot, $projdir );
					my $filegroupText = elemText( $targetChild );
					my @groupLines = split( /\n/, $filegroupText );
					my $filegroupHash = $newTarget->{filegroups};
					my $filegroup = $filegroupHash->{$fileGroupName};
					if ( !$filegroup ) {
						$filegroup = { name => $fileGroupName, root => $fileGroupRoot, files => {} };
						$filegroupHash->{$fileGroupName} = $filegroup;
					}
					my $newFiles = $filegroup->{files};
					foreach my $line ( @groupLines ) {
						my ($negated, $dir, $filespec) = $line =~ /\s*(-?)(.*?[\/\\]?)([^\/\\\s]+)\s*$/;
						if ( $filespec ) {
							#if negated, remove the files from the existing group that match a given pattern.
							#if not negated, then add the files 
							if ( !$negated ) {
								my $globbed = $filespec =~ /\*/;
								if ( $globbed ) {
									my $files = match_files_to_glob( catdir( $actualRoot, $dir ), $filespec );
									foreach my $file (@$files) {
										if ( !( -f $file ) ) {
											die "Internal error - match files to glob returned directory $file, $actualRoot, $dir, $filespec";
										}
										my $relpath = File::Spec::->abs2rel( $file, $projdir );
										$newFiles->{$relpath} = $file;
									}
								}
								else {
								
									my $fname = catfile( catdir( $actualRoot, $dir ), $filespec );
									if ( !( -f $fname ) ) {
										die "Target file $fname doesn't exist or isn't a file";
									}
									my $relpath = File::Spec::->abs2rel( $fname, $projdir );
									$newFiles->{$relpath} = $fname;
								}
							}
							else {
								
								#negated files have to be listed by hand, globbing isn't supported.
								my $fname = catfile( catdir( $actualRoot, $dir ), $filespec );
								if ( -f $fname ) {
									my $relpath = File::Spec::->abs2rel( $fname, $projdir );
									delete $newFiles->{ $relpath };
								}
							}
						}
					}
				}
				elsif( $nodeName eq "depends" ) {
					my $dependsText = elemText( $targetChild );
					my @groupLines = split( /\n/, $dependsText );
					my $dependsHash = $newTarget->{depends};
					foreach my $line (@groupLines) {
						my $trimmed = trim($line);
						$dependsHash->{$trimmed} = 1;
					}
				}
				else {
					die "Unrecognized target child $nodeName" unless process_target_config_child( $om, $newTarget, $targetChild );
				}
			}
		}
	}
}

sub set_projects {
	my ($self, $projects) = @_;
	$self->{_projects} = $projects;
}

sub get_projects {
	my $self = shift;
	return $self->{_projects};
}


sub get_target_nodes_by_type {
	my ($om, $target, $nodeType) = @_;
	my $nodes = $target->{nodes};
	my $retval = [];

	foreach my $node (@$nodes) {
		my $nodeTypeName = $node->{type};
		if ( $node->{type} eq $nodeType ) {
			push( @$retval, $node->{values} );
		}
	}
	return $retval;
}


sub get_target_and_config_nodes_by_type {
	my ($om, $target, $configName, $nodeType) = @_;
	my $nodes = $target->{nodes};
	my $retval = [];
	
	foreach my $node (@$nodes) {
		my $nodeTypeName = $node->{type};
		if ( $node->{type} eq $nodeType ) {
			push( @$retval, $node->{values} );
		}
		elsif( $node->{type} eq "configuration" ) {
			my $configValues = $node->{values};
			my $thisConfigName = $configValues->{name};
			if ( $thisConfigName eq $configName ) {
				my $confNodes = $om->get_target_nodes_by_type( $configValues, $nodeType );
				foreach my $confNode (@$confNodes ) {
					push( @$retval, $confNode );
				}
			}
		}
	}
	return $retval;
}
#properties are things that have a distinct value and a later definition overrides an earlier
#definition

sub get_target_property_no_default {
	my ($om, $target, $configName, $propname) = @_;
	my $properties = get_target_and_config_nodes_by_type( $om, $target, $configName, "property" );
	my $retval;
	foreach my $prop (@$properties) {
		my @propkeys = keys $prop;
		my $propName = $prop->{name};

		if ( $prop->{name} eq $propname ) {
			$retval = $prop->{value};
		}
	}
	return $retval;
}
sub get_target_property {
	my ($om, $target, $configName, $propname) = @_;
	my $retval = get_target_property_no_default( $om, $target, $configName, $propname);
	if ( !$retval ) {
		my $compilation_properties = $om->{compilation_properties};
		my $propertyDefinition = $compilation_properties->{$propname};
		if ( !$propertyDefinition ) {
			die "Unrecognized property $propname";
		}
		$retval = $propertyDefinition->{default};
		if ( !$retval ) {
			die "Property $propname wasn't specified and has no default";
		}
	}
	return $retval;
}

sub get_target_precompiled_header {
	my ($om, $target, $configName) = @_;
	return $om->get_target_property_no_default( $target,  $configName, "precompiled-header" );
}

sub append_precomp_header
{
	my ( $precompiledHeaders, $header, $conf ) = @_;
	if ( $header ) {
		my $array = $precompiledHeaders->{$header};
		if ( !$array ) {
			$array = [];
			$precompiledHeaders->{$header} = $array;
		}
		push( @$array, $conf );
	}
}

# - return a map from file to configuration name where this file is a precompiled header.
sub get_target_precompiled_header_map {
	my ($om, $target) = @_;
	my $precompiledHeaders = {};
	my $targetConfigs = $om->get_configuration_names( $target );
	foreach my $conf (@$targetConfigs) {
		my $precompHeader = $om->get_target_precompiled_header( $target, $conf );
		if ( $precompHeader ) {
			my $header = $precompHeader->{header};
			my $source = $precompHeader->{source};
			if ( $header ) {
				append_precomp_header( $precompiledHeaders, $header, $conf );
				append_precomp_header( $precompiledHeaders, $source, $conf );
			}
		}
	}
		
	return $precompiledHeaders;
}

sub get_target_out_dir {
	my ($om, $target, $configname ) = @_;
	my $configType = $om->get_target_property( $target, $configname, "configuration-type" );
	my $project = $target->{project};
	my $outDir = $project->{binary_directory};
	if ( $configType  eq "static-library" ) {
		$outDir = $project->{library_directory};
	}
	return $outDir;
}

#path lists are header and linker path lists so far
sub get_path_list {
	my ($om, $target, $configname, $listname) = @_;
    $listname = "$listname-search-paths";
	#this should be an array of arrays
	my $node_values = get_target_and_config_nodes_by_type( $om, $target, $configname, $listname );
	my @retval;
	my $found_items = {};
	foreach my $node_value (@$node_values)  {
		foreach my $path (@$node_value) {
			if ( !$found_items->{$path} ) {
				push( @retval, $path );
				$found_items->{$path} = 1;
			}
		}
	}
	if ( $listname eq "linker-search-paths" ) {
		my $project = $target->{project};
		my $targets = $project->{targets};
		my $depends = $target->{depends};
		foreach my $depend (keys %$depends) {
			foreach my $subTarget (@$targets) {
				my $subName = $subTarget->{name};
				if ( $subName eq $depend ) {
					my $outDir = $om->get_target_out_dir( $subTarget, $configname );
					if ( !$found_items->{$outDir} ) {
						push( @retval, $outDir );
						$found_items->{$outDir} = 1;
					}
				}
			}
		}
	}
	#don't even think of sorting them.  Header/linker include lists are extremely sensitive to ordering
	return \@retval;
}

sub get_target_input_libraries 
{
	my ($om, $target, $configname, $retval, $found_items) = @_;
	my $node_values = get_target_and_config_nodes_by_type( $om, $target, $configname, "libraries" );
	foreach my $node_value (@$node_values)  {
		foreach my $path (@$node_value) {
			if ( !$found_items->{$path} ) {
				push( @$retval, $path );
				$found_items->{$path} = 1;
			}
		}
	}
}

sub get_target_depends_libraries 
{
	my ($om, $target, $configname, $retval, $found_items, $fullpath) = @_;
	my $found_items = {};

	my $project = $target->{project};
	my $targets = $project->{targets};
	my $depends = $target->{depends};
	foreach my $depend (keys %$depends) {
		foreach my $subTarget (@$targets) {
			my $subName = $subTarget->{name};

			if ( $subName eq $depend ) {
				my $artifact_name = $om->get_target_property( $subTarget, $configname, "artifact-name" );
				my $artifact_fullpath = catfile( $om->get_target_out_dir( $subTarget, $configname ), $artifact_name );
				if ( !$found_items->{$artifact_fullpath} ) {
					if ( $fullpath ) {
						push( @$retval, $artifact_fullpath );
					}
					else {
						push( @$retval, $artifact_name );
					}
					$found_items->{$artifact_fullpath} = 1;
				}
			}
		}
	}
}


sub get_target_depends_libraries_full_path
{
	my ($om, $target, $configname, $retval, $found_items) = @_;
	return $om->get_target_depends_libraries( $target, $configname, $retval, $found_items, 1 );
}


sub get_target_libraries {
	my ($om, $target, $configname) = @_;
	my $foundLibs = {};
	my $retval = [];
	get_target_input_libraries( $om, $target, $configname, $retval, $foundLibs );
	get_target_depends_libraries( $om, $target, $configname, $retval, $foundLibs );
	return $retval;
}

sub get_configuration_names {
	my ($self, $target) = @_;
	my $configs = $self->get_target_nodes_by_type( $target, "configuration" );
	my $retval = [];
	my $foundConfigs = {};
	foreach my $item (@$configs) {
		my $confName = $item->{name};
		if ( !($foundConfigs->{$confName}) ) {
			push( @$retval, $confName );
			$foundConfigs->{$confName} = 1;
		}
	}
	return $retval;
}

sub get_target_files {
	my ($self, $target) = @_;
	my $filegroups = $target->{filegroups};
	my @retval;
	my $foundFiles = {};
	foreach my $groupname (keys %$filegroups) {
		my $group = $filegroups->{$groupname};
		#group files is a hash of project relative path to full path
		my $groupFileHash = $group->{files};
		my @groupFileList = keys %$groupFileHash;
		foreach my $groupFile (@groupFileList) {
			if ( !$foundFiles->{$groupFile} ) {
				push( @retval, $groupFile );
				$foundFiles->{$groupFile} = 1;
			}
		}
	}
	my @sorted = sort( @retval );
	return \@sorted;
}

sub get_project_configuration_names 
{
	my ($om, $project) = @_;
	my $targets = $project->{targets};
	my $confHash = {};
	my $confList = [];
  	foreach my $target (@$targets) {
		my $targetConfigs = $om->get_configuration_names( $target );
		foreach my $targetConf (@$targetConfigs) {
			if ( !$confHash->{$targetConf} ) {
				push( @$confList, $targetConf );
				$confHash->{$targetConf} = 1;
			}
		}
	}
	return $confList;
}


my $FILE_TYPE_COMPILE = 1;
my $FILE_TYPE_HEADER = 2;
my $FILE_TYPE_UNKNOWN = 3;

sub get_file_type
{
	my ($om, $file) = @_;
	$file = lc($file);
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

sub build_object_model
{
	my ($self, $doc) = @_;
	my $docelem = $doc->documentElement;
	my @children = $docelem->childNodes;
	my $projects = [];
	foreach my $child (@children) {
		if ( $child->nodeType == XML_ELEMENT_NODE ) {
			my $nodeName = $child->nodeName;
			if ( $nodeName eq "project" ) {
				my $projName = $child->getAttribute( "name" );
				my $newProj = { name => $projName, targets => [] };
				unshift( @$projects, $newProj );
				my @projChildren = $child->childNodes;
				foreach my $projChild (@projChildren) {
					if ( $projChild->nodeType == XML_ELEMENT_NODE ) {
						process_project_children( $self, $newProj, $projChild );
					}
				}
			}
		}
	}
	$self->set_projects( $projects );
}

sub extend_compilation_properties
{
	my ($self,$properties) = @_;
	my $compilation_properties = $self->{compilation_properties};
	foreach my $key (keys %$properties ) {
		$compilation_properties->{$key} = $properties->{$key};
	}
}

sub new
{
	my $class = shift;
	my $self = { compilation_properties=>$initial_compilation_properties
					 , FILE_TYPE_COMPILE => $FILE_TYPE_COMPILE
					 , FILE_TYPE_HEADER => $FILE_TYPE_HEADER
					 , FILE_TYPE_NONE => $FILE_TYPE_UNKNOWN };
	bless $self, $class;
	return $self;
}

1;
