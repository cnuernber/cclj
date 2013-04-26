use File::Basename;
use File::Spec::Functions;
use XML::LibXML;
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

sub process_project_children {
	my $project = shift;
	my $projChild = shift;
	my $name = $projChild->nodeName;
	if ( $name eq "projdir" ) {
		$projdir = catdir( getcwd(), elemText( $projChild ) );
		mkpath( $projdir );
		$projdir = realpath( $projdir );
		$project->{project_directory} = $projdir;
	}
	if ( $name eq "bindir" ) {
		$bindir = project_relative_dir( $projChild );
	}
	elsif ( $name eq "libdir" ) {
		$libdir = project_relative_dir( $projChild );
	}
	elsif ( $name eq "intdir" ) {
		$intdir = project_relative_dir( $projChild );
	}
	elsif( $name eq "target" ) {
		
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
				my $newProj = { name => $projName };
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
		print( "found project " . $proj->{name} . "\n" );
	}
}


return { process=>\&process };
