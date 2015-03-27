GPUswap
=======

GPUswap is a modification to the [pscnv GPU
driver](https://github.com/pathscale/pscnv), which enables transparent
oversubscription of GPU memory. If applications allocate more GPU
memory than available, some application data is transparently moved to
system RAM, allowing applications to allocate arbitrary amounts of
memory. Since moving data to system RAM causes performance
degradation, GPUswap also balances the available GPU memory between
applications. For more information, please see [our
paper](http://os.itec.kit.edu/english/21_2998.php) at VEE '15.

Building
========

GPUswap is built along with the rest of pscnv. No special action is
required. See [README.pscnv](README.pscnv) for details.

LICENSE
=======

1. All files in this repository are licensed under the
   [CRAPL v0 BETA 1](CRAPL-LICENSE.txt).
2. If the CRAPL is incompatible with a pre-existing license on a file,
   that file, including all changes made by us, remains licensed under
   its original license.
3. If a file is covered by a pre-existing license that is compatible
   with the CRAPL, the original license stays in effect in addition to
   the CRAPL.

If these terms violate anyone's rights, please contact the authors so
we can resolve the issue.

BUGS
====

We know lots of them are out there :) If you encounter any, please
feel free (but not obliged) to submit bug reports or patches through
GitHub's bug tracker.
