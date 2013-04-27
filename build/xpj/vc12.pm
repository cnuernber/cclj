use File::Basename;
use File::Spec::Functions;
use XML::LibXML;
use Data::UUID;
use Cwd qw(realpath getcwd);

my $xpj;
my $projects = [];
my $bindir;
my $libdir;
my $intdir;
my $projdir;

sub elemText {
	my $elem = shift;
	my $retval = "";
	my @childNodes = $elem->childNodes;
	foreach my $child (@childNodes) {
		$nodeType = XML_ELEMENT_TEXT;
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

sub process_target_config_child {
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
				$fullpath = catdir( getcwd(), $trimmed );
				if ( !( -e $fullpath ) ) {
					die "Search path $line does not exist";
				}
				$fullpath = realpath( $fullpath );
				$relpath = File::Spec->abs2rel( $fullpath, $projdir );
				push( @$resultArray, $relpath );
			}
		}
	}
	elsif( $nodeName eq "cflags" || $nodeName eq "lflags" || $nodeName eq "preprocessor" ) {
		$resultArray = add_array_node( $parent, $nodeName );
		my @lines = split( /\n/, elemText( $elem ) );
		foreach my $line (@lines ) {
			my $trimmed = trim( $line );
			if ( $trimmed ) {
				push( @$resultArray, $trimmed );
			}
		}
	}
	else {
		return 0;
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
				push( @$retval, $localfile );
			}
		}
	}
	closedir( $dirhandle );
	return $retval;
}

sub process_project_children {
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
		$bindir = project_relative_dir( $projChild );
		$project->{binary_directory} = $bindir;
	}
	elsif ( $name eq "libdir" ) {
		$libdir = project_relative_dir( $projChild );
		$project->{library_directory} = $bindir;
	}
	elsif ( $name eq "builddir" ) {
		$intdir = project_relative_dir( $projChild );
		$project->{build_directory} = $bindir;
	}
	elsif( $name eq "target" ) {
		my $uid = $ug->to_string( $ug->create() );

		my $newTarget = { name => $projChild->getAttribute( "name" ), uid => $uid, filegroups => {}, project => $project };
		my $targets = $project->{targets};
		push( @$targets, $newTarget );
		my @targetChildren = $projChild->childNodes;
		foreach my $targetChild (@targetChildren) {
			if ( $targetChild->nodeType == XML_ELEMENT_NODE ) { 
				
				$nodeName = $targetChild->nodeName;
				if ( $nodeName eq "configuration" ) {
					#get or create config by name
					my $configName = $targetChild->getAttribute( "name" );
					my $configuration = add_hash_node( $newTarget, "configuration" );
					$configuration->{configType} = $targetChild->getAttribute( "configType" );
					
					my @configChildren = $targetChild->childNodes;
					foreach my $configChild (@configChildren) {
						if ( $configChild->nodeType == XML_ELELMENT_NODE ) { 
							my $configChildNodeName = $configChild->nodeName;
							if ( $configChildNodeName eq "set-artifact-name" ) {
								$configuration->{"artifact-name"} = elemText( $configChild );
							}
							else {
								die "Unrecognized configuration child $configChildNodeName" 
									unless process_target_config_child( $configuration, $configChild );
							}
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
						my ($negate, $dir, $filespec) = $line =~ /\s*(-?)(.*?[\/\\]?)([^\/\\\s]+)\s*$/;
						#if negated, remove the files from the existing group that match a given pattern.
						#if not negated, then add the files 
						if ( !$negated ) {
							my $globbed = $filespec =~ /\*/;
							if ( $globbed ) {
								my $files = match_files_to_glob( catdir( $actualRoot, $dir ), $filespec );
								foreach my $file (@$files) {
									$relpath = File::Spec::->abs2rel( $file, $projdir );
									$newFiles {$relpath} = $file;
								}
							}
							else {
								$fname = catfile( catdir( $actualRoot, $dir ), $filespec );
								if ( !( -e $fname ) ) {
									die "Target file $fname doesn't exist";
								}
								$relpath = File::Spec::->abs2rel( $fname, $projdir );
								$newFiles {$relpath} = $file;
							}
						}
						else {
							#negated files have to be listed by hand, globbing isn't supported.
							$fname = catfile( catdir( $actualRoot, $dir ), $filespec );
							if ( -e $fname ) {
								$relpath = File::Spec::->abs2rel( $fname, $projdir );
								delete $newFiles{ $relpath };
							}
						}
					}
				}
				else {
					die "Unrecognized target child $nodeName" unless process_target_config_child( $newTarget, $targetChild );
				}
			}
		}
	}
}

sub process
{
	$xpj = shift;
	my $doc = shift;
	my $docelem = $doc->documentElement;
	my @children = $docelem->childNodes;
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
						process_project_children( $newProj, $projChild );
					}
				}
			}
		}
	}
	foreach my $proj (@$projects) {
	
		my $targets = $proj->{targets};
		foreach my $target (@$targets) {
			foreach my $targetKey (keys %$target ) {
				my $targetValue = $target->{$targetKey};
		
			}

		}
	}
}


return { process=>\&process };
