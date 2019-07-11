RPM_SOURCE_DIR=`rpmbuild -E '%{_sourcedir}'`
VERSION=0.1.3

mkdir -p "$RPM_SOURCE_DIR"
tar -cvf "$RPM_SOURCE_DIR/symrouted-$VERSION.tgz" --show-stored-names --transform "s,^src,symrouted-$VERSION," -C `pwd` src
rpmbuild --define "version $VERSION" -ba symrouted.spec
